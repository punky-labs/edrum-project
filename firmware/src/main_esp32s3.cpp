// XIAO ESP32-S3 eDrum firmware (head unit)
//
// Sensing rewrite Stage 1: the analogRead() sampling loop and global ring_buffer
// are replaced by the 3-layer pipeline
//     AdcSampler (DMA) -> SampleStream (ring + demux) -> PDrum2Trigger -> MIDI.
// The sensing layer is accessed only through the TriggerEngine interface.
// USB / TinyUSB / Serial setup is unchanged.

#include <Arduino.h>
#include <LittleFS.h>
#undef FILE_READ
#undef FILE_WRITE
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <Adafruit_TinyUSB.h>
#pragma GCC diagnostic pop
#include <MIDI.h>

#include "config/Config.h"
#include "midi/SysEx.h"
#include "sensing/TriggerEngine.h"
// Phase-1 revert: the simple HelloDrum-derived PDrumTrigger is the active engine.
// PDrum2Trigger (Edrumulus, Stage 2a) stays in the tree, unused, for a future return.
#include "sensing/pdrum/PDrumTrigger.h"
#include "sensing/sampling/AdcSampler.h"
#include "sensing/sampling/SampleStream.h"

// Dev tooling (dev-build only — no-ops when DEV_BUILD is undefined). Replaces the
// dead USB-serial-RX debug path with a WiFi telnet console; reads dev.txt flags.
#include "dev/DevConfig.h"
#include "dev/DevLog.h"
#include "dev/DevWiFi.h"

// FW_BUILD is injected by the RP2040 build's increment_build.py extra_script.
// This env does not run that script, so provide a fallback so printHelp() builds.
#ifndef FW_BUILD
#define FW_BUILD 0
#endif

// ---------------------------------------------------------------------------
// USB MIDI
// ---------------------------------------------------------------------------

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// ---------------------------------------------------------------------------
// Config apply/save request flags (set by SysEx handlers, serviced in loop)
// ---------------------------------------------------------------------------

volatile bool g_save_requested  = false;
volatile bool g_apply_requested = false;

// ---------------------------------------------------------------------------
// Sampling pipeline (Layers 1 + 2)
// ---------------------------------------------------------------------------

#define ADC_PRINT_FLOOR 10

// All 8 ADC1 channels (head unit), in frame order. SampleStream channel index
// == position in this array; GPIO -> stream-channel index is (gpio - kChannelGpios[0]).
static const uint8_t kChannelGpios[8] = { 2, 3, 4, 5, 6, 7, 8, 9 };

static AdcSampler   sampler;
static SampleStream stream;

// Per-input read cursors into the stream (one consumer = one cursor pair).
static SampleStream::Cursor headCursor[NUM_INPUTS];
static SampleStream::Cursor rimCursor[NUM_INPUTS];

// Samples pulled per channel per loop iteration. Loop runs far faster than the
// 8 kHz sample rate, so n is normally tiny; this is just the per-call ceiling.
static const uint16_t kBlock = 256;

// ---------------------------------------------------------------------------
// Trigger engine instances
// ---------------------------------------------------------------------------

static TriggerEngine* triggers[NUM_INPUTS];

// ADC channel per input: {headGpio, rimGpio}; -1 = no channel (hi-hat stub).
// Jacks 0-3: head/piezo + rim; Jack 4: hi-hat controller — stubbed.
// NOTE (2026-07-06): swapped from the original {2,4,6,8}/{3,5,7,9} assignment —
// confirmed via single-piezo pads (KD-80, which cannot produce a rim signal at
// all) on jacks 0, 1, and 3 that real signal consistently lands on the HIGHER
// GPIO of each pair, not the lower one. The ESP32-S3 GPIO-to-ADC1-channel
// mapping itself is confirmed correct against the datasheet (GPIO2->CH1,
// GPIO3->CH2, etc.) — this was a head/rim labelling swap, not an ADC bug.
static const int8_t kHeadCh[NUM_INPUTS] = { 3, 5, 7, 9, -1 };
static const int8_t kRimCh[NUM_INPUTS]  = { 2, 4, 6, 8, -1 };

// Map a GPIO number to its SampleStream channel index (-1 if no channel).
static inline int streamCh(int8_t gpio) {
    return (gpio < 0) ? -1 : (int)gpio - (int)kChannelGpios[0];
}

static void applyConfig() {
    for (int i = 0; i < NUM_INPUTS; i++) {
        if (!triggers[i]) continue;
        triggers[i]->setPadType(g_inputs[i].padType);
        triggers[i]->setRimRatioThreshold(g_inputs[i].rimRatioThreshold);
        triggers[i]->setChokeThreshold(g_inputs[i].chokeThreshold);
        triggers[i]->setChokeEnabled(g_inputs[i].chokeEnabled);
        triggers[i]->setRetriggerTime(g_inputs[i].retriggerTime);
        triggers[i]->setHeadThreshold(g_inputs[i].threshold);
        triggers[i]->setHeadSensitivity(g_inputs[i].headSensitivity);
        triggers[i]->setScanTime(g_inputs[i].scanTime);
        triggers[i]->setMaskTime(g_inputs[i].maskTime);
        triggers[i]->setCurveType(g_inputs[i].velocityCurve);
        triggers[i]->setNoteHead(g_inputs[i].midiNote);
        // Tier-2 Edrumulus params (Stage 2a)
        triggers[i]->setPreScanTimeMs(g_inputs[i].preScanTimeMs);
        triggers[i]->setFirstPeakDiffThreshDb(g_inputs[i].firstPeakDiffThreshDb);
        triggers[i]->setDecayLen1Ms(g_inputs[i].decayLen1Ms);
        triggers[i]->setDecayGradFact1(g_inputs[i].decayGradFact1);
        triggers[i]->setDecayLen2Ms(g_inputs[i].decayLen2Ms);
        triggers[i]->setDecayGradFact2(g_inputs[i].decayGradFact2);
        triggers[i]->setDecayLen3Ms(g_inputs[i].decayLen3Ms);
        triggers[i]->setDecayGradFact3(g_inputs[i].decayGradFact3);
        triggers[i]->setDecayFactDb(g_inputs[i].decayFactDb);
        triggers[i]->setMaskTimeDecayFactDb(g_inputs[i].maskTimeDecayFactDb);
        triggers[i]->setDecayEstDelayMs(g_inputs[i].decayEstDelayMs);
        triggers[i]->setDecayEstLenMs(g_inputs[i].decayEstLenMs);
        triggers[i]->setDecayEstFactDb(g_inputs[i].decayEstFactDb);
        triggers[i]->setClipCompAmpmapStep(g_inputs[i].clipCompAmpmapStep);
    }
}

// Exposed for SysEx.cpp's preset handlers: presetLoad/Save/Delete do synchronous
// LittleFS I/O directly in the SysEx receive path (not deferred via a flag), so they
// bracket those calls with these to keep a filesystem-write stall from overflowing
// the ADC store buffer — same protection loop() gives configSave(). Plain extern
// functions (declared in SysEx.h) to match the g_save_requested house style; `sampler`
// is file-static so the bodies must live here where it's in scope.
void adcSamplerPause()  { sampler.pause();  }
void adcSamplerResume() { sampler.resume(); }

// ---------------------------------------------------------------------------
// SysEx USB bridge
// BleMidi.cpp is excluded from this build; provide stubs here so that
// SysEx.cpp's call to bleMidiSendSysEx() routes through USB MIDI instead.
// ---------------------------------------------------------------------------

void usbMidiSendSysEx(const uint8_t* data, size_t len) {
    MIDI.sendSysEx((unsigned)len, data, true);
}

void bleMidiSendSysEx(const uint8_t* data, size_t len) { usbMidiSendSysEx(data, len); }
bool bleMidiIsConnected() { return false; }
void bleMidiInit()        {}
void bleMidiPoll()        {}

// ---------------------------------------------------------------------------
// MIDI SysEx receive callback
// ---------------------------------------------------------------------------

static void onSysEx(byte* data, unsigned size) {
    // TinyUSB passes the full framed message including F0/F7
    // Strip them before passing to sysexParse
    if (size < 2 || data[0] != 0xF0 || data[size-1] != 0xF7) return;
    sysexParse(data + 1, size - 2);
}

// ---------------------------------------------------------------------------
// Serial debug commands
// ---------------------------------------------------------------------------

static void printHelp() {
    DevLog.printf("[eDrum] Build %d — p=ping  i=identify  s=config  n=test note  r=reboot\n", FW_BUILD);
    DevLog.println("  a=toggle ADC dump  d=toggle hit debug print  m=toggle diag mode (detection+MIDI off)");
    DevLog.println("  o <input> <floor> = scope input (e.g. o 0 10)   o off = disable scope");
    DevLog.println("  w <input> <param> <value> = set param (e.g. w 0 scan 3)");
    DevLog.println("  params: thresh sens scan mask retrig type ratio chokethresh choke enable");
}

static bool g_adcDump = false;  // ADC auto-dump off by default now (was on for
                                // bring-up). Detection runs normally; re-enable
                                // via 'a' only when needed (and only meaningful
                                // in MODE=1 diag firmware where serial RX works).
bool g_serialQuiet = false;
// DIAGNOSTIC: gate hit serial output AND hit-debug SysEx so the serial link is
// quiet enough to use 'a' and inspect the raw signal. Toggle with 'd'.
// Default OFF = no hit spam. MIDI note output is unaffected (still sent).
static bool g_hitDebug = false;

// DIAGNOSTIC MODE: when true, loop() ONLY pumps the stream and services the 'a'
// ADC dump. The entire per-input detection + MIDI + SysEx block is skipped, so
// NO MIDI is sent and NO detection runs. This isolates the sampling pipeline so
// we can read the raw ADC baseline over a quiet serial link (the USB MIDI flood
// was starving serial RX). Toggle with 'm'.
// Default OFF now: bring-up diagnosis complete (floating jacks + stale cache were
// the phantom-hit causes). Normal detection runs from boot.
static bool g_diagMode = false;

// Scope state
static bool     g_scopeActive  = false;
static uint8_t  g_scopeInput   = 0;
static uint16_t g_scopeFloor   = 10;
static bool     g_scopePending = false;
static uint32_t g_scopeSnap    = 0;
static bool     g_scopeIsRim   = false;

static void handleSerial(char cmd) {
    switch (cmd) {
        case 'p': {
            uint8_t msg[] = { 0xF0, SYSEX_MFR_0, SYSEX_MFR_1, SYSEX_DEV_HEAD,
                              SYSEX_CAT_SYS, SYSEX_SYS_PING, 0xF7 };
            usbMidiSendSysEx(msg, sizeof(msg));
            DevLog.println("[>] SysEx ping sent");
            break;
        }
        case 'i': {
            uint8_t msg[] = { 0xF0, SYSEX_MFR_0, SYSEX_MFR_1, SYSEX_DEV_HEAD,
                              SYSEX_CAT_SYS, SYSEX_SYS_IDENT_REQ, 0xF7 };
            usbMidiSendSysEx(msg, sizeof(msg));
            DevLog.println("[>] SysEx identify sent");
            break;
        }
        case 's': {
            DevLog.println("[Config]");
            for (int i = 0; i < NUM_INPUTS; i++) {
                DevLog.printf("  [%d] en=%d type=%d note=%d ch=%d z2note=%d z2ch=%d"
              " thresh=%d sens=%d scan=%d mask=%d"
              " ratio=%d chokethresh=%d choke=%d curve=%d retrig=%d"
              " seeds=%d builds=%d\n",
                    i,
                    (int)g_inputs[i].enabled,
                    g_inputs[i].padType,
                    g_inputs[i].midiNote,    g_inputs[i].midiChannel,
                    g_inputs[i].zone2MidiNote, g_inputs[i].zone2MidiChannel,
                    g_inputs[i].threshold,   g_inputs[i].headSensitivity,
                    g_inputs[i].scanTime,    g_inputs[i].maskTime,
                    g_inputs[i].rimRatioThreshold, g_inputs[i].chokeThreshold,
                    (int)g_inputs[i].chokeEnabled,
                    g_inputs[i].velocityCurve, g_inputs[i].retriggerTime,
                    triggers[i] ? triggers[i]->getDebugSeedCount()  : -1,
                    triggers[i] ? triggers[i]->getDebugBuildCount() : -1);
            }
            break;
        }
        case 'n': {
            // C3 = MIDI note 48
            MIDI.sendNoteOn(48, 100, 10);
            MIDI.sendNoteOff(48, 0, 10);
            DevLog.println("[>] Note C3 vel=100 ch=10");
            break;
        }
        case 'r': {
            DevLog.println("[eDrum] Restarting...");
            delay(100);
            ESP.restart();
            break;
        }
        case 'a': {
            g_adcDump = !g_adcDump;
            if (g_adcDump && g_scopeActive) {
                g_scopeActive  = false;
                g_scopePending = false;
                DevLog.println("[SCOPE] Warning: scope disabled — ADC dump active");
            }
            g_serialQuiet = g_adcDump;
            DevLog.println(g_adcDump ? "[ADC] Dump ON" : "[ADC] Dump OFF");
            break;
        }
        case 'd': {
            g_hitDebug = !g_hitDebug;
            DevLog.println(g_hitDebug ? "[DBG] hit output ON" : "[DBG] hit output OFF (quiet)");
            break;
        }
        case 'm': {
            g_diagMode = !g_diagMode;
            DevLog.println(g_diagMode
                ? "[DIAG] diagnostic mode ON  (detection+MIDI disabled, ADC dump only)"
                : "[DIAG] diagnostic mode OFF (normal detection+MIDI)");
            break;
        }
        case 'o': {
            String args = DevLog.readLine();
            args.trim();
            if (args.length() == 0) {
                DevLog.println("[SCOPE] Usage: o <input> <floor>  |  o off");
            } else if (args.startsWith("off")) {
                g_scopeActive  = false;
                g_scopePending = false;
                DevLog.println("[SCOPE] Disabled");
            } else {
                int inp = -1, flr = 10;
                if (sscanf(args.c_str(), "%d %d", &inp, &flr) >= 1
                        && inp >= 0 && inp < NUM_INPUTS) {
                    g_scopeActive  = true;
                    g_scopeInput   = (uint8_t)inp;
                    g_scopeFloor   = (uint16_t)flr;
                    g_scopePending = false;
                    DevLog.printf("[SCOPE] Active: input=%d floor=%d\n", inp, flr);
                } else {
                    DevLog.printf("[SCOPE] Error: input must be 0-%d\n", NUM_INPUTS - 1);
                }
            }
            break;
        }
        case 'h': {
            printHelp();
            break;
        }
        case 'w': {
            String args = DevLog.readLine();
            args.trim();
            int inp = -1, val = -1;
            char param[16] = {};
            if (sscanf(args.c_str(), "%d %15s %d", &inp, param, &val) == 3
                    && inp >= 0 && inp < NUM_INPUTS && val >= 0) {
                String p = String(param);
                bool ok = true;
                if      (p == "thresh")     { g_inputs[inp].threshold         = (uint16_t)val; }
                else if (p == "sens")       { g_inputs[inp].headSensitivity   = (uint16_t)val; }
                else if (p == "scan")       { g_inputs[inp].scanTime          = (uint16_t)val; }
                else if (p == "mask")       { g_inputs[inp].maskTime          = (uint16_t)val; }
                else if (p == "retrig")     { g_inputs[inp].retriggerTime     = (uint16_t)val; }
                else if (p == "type")       { g_inputs[inp].padType           = (uint8_t)val;  }
                else if (p == "ratio")      { g_inputs[inp].rimRatioThreshold = (uint16_t)val; }
                else if (p == "chokethresh"){ g_inputs[inp].chokeThreshold    = (uint16_t)val; }
                else if (p == "choke")      { g_inputs[inp].chokeEnabled      = (bool)val;     }
                else if (p == "enable")     { g_inputs[inp].enabled          = (bool)val;     }
                else { DevLog.printf("[w] Unknown param '%s'\n", param); ok = false; }
                if (ok) {
                    // Defer apply+save to loop() (single pause/resume-bracketed path,
                    // same as the SysEx PAD_SET_* handlers) instead of applying inline
                    // here — an inline applyConfig()+LUT rebuild could stall the loop
                    // and overflow the ADC store buffer.
                    g_apply_requested = true;
                    g_save_requested  = true;
                    DevLog.printf("[w] input=%d %s=%d OK\n", inp, param, val);
                }
            } else {
                DevLog.println("[w] Usage: w <input> <param> <value>");
                DevLog.println("[w] params: thresh sens scan mask retrig type ratio chokethresh choke enable");
            }
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Helper: map raw ADC value to 0-127, mirroring pdrum curve() map()
// ---------------------------------------------------------------------------

static uint8_t rawToMidi(int raw, uint16_t threshold, uint16_t sens) {
    if (raw <= (int)threshold) return 0;
    long mapped = map((long)raw, (long)threshold, (long)sens, 1, 127);
    if (mapped < 0)   return 0;
    if (mapped > 127) return 127;
    return (uint8_t)mapped;
}

// ---------------------------------------------------------------------------
// Scope capture dump — called after 100 post-hit samples have accumulated.
// Reads the 100-pre / 100-post window through SampleStream::readWindow().
// Output format is identical to before so the desktop ADC Scope tool still parses.
// ---------------------------------------------------------------------------

static void scopeDump(int input, bool isRim) {
    int hc = streamCh(kHeadCh[input]);
    int rc = streamCh(kRimCh[input]);

    static const uint32_t PRE   = 100;
    static const uint32_t POST  = 100;
    static const uint32_t TOTAL = PRE + POST;

    uint16_t headWin[TOTAL] = {};
    uint16_t rimWin[TOTAL]  = {};
    // SAFETY: callers guard g_scopeSnap >= PRE, but clamp here too (shared fn,
    // unsigned math). If the snap is somehow < PRE, start at 0 rather than wrap.
    uint32_t start = (g_scopeSnap >= PRE) ? (g_scopeSnap - PRE) : 0;
    if (hc >= 0) stream.readWindow((uint8_t)hc, start, headWin, (uint16_t)TOTAL);
    if (rc >= 0) stream.readWindow((uint8_t)rc, start, rimWin,  (uint16_t)TOTAL);

    int headPeak = 0, rimPeak = 0;
    for (uint32_t t = 0; t < TOTAL; t++) {
        if (headWin[t] > headPeak) headPeak = headWin[t];
        if (rimWin[t]  > rimPeak)  rimPeak  = rimWin[t];
    }

    DevLog.printf("[SCOPE] input=%d pad_type=%d head_ch=%d rim_ch=%d head_peak=%d rim_peak=%d decision=%s samples=%d\n",
        input, (int)g_inputs[input].padType, hc, rc, headPeak, rimPeak, isRim ? "RIM" : "HEAD", (int)TOTAL);
    DevLog.println("T,H,R");
    for (uint32_t t = 0; t < TOTAL; t++) {
        DevLog.printf("%d,%d,%d\n", (int)t, (int)headWin[t], (int)rimWin[t]);
    }
}

// ---------------------------------------------------------------------------
// setup / loop — USB MIDI, config, sampling pump, sensing, output
// ---------------------------------------------------------------------------

void setup() {
    if (!TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.begin(0);
    }
    usb_midi.setStringDescriptor("eDrum");
    MIDI.begin(MIDI_CHANNEL_OMNI);
    MIDI.setHandleSystemExclusive(onSysEx);
    Serial.begin(115200);
    delay(5000);
    // Command line-reads ('o'/'w') now come over telnet via DevLog.readLine(),
    // which has its own short internal timeout — so Serial's timeout no longer
    // gates the loop. Kept small anyway as a belt-and-braces guard on any stray
    // Serial read: a blocking read must never stall pump()/the input drain.
    Serial.setTimeout(20);
    Serial.println("[eDrum] Ready.");
    // BUILD STAMP: __DATE__/__TIME__ are set by the compiler at build time, so this
    // line changes on every real rebuild. If you flash and this timestamp is NOT
    // newer than your last build, you flashed a STALE binary (pioarduino cache) —
    // do a full clean (pio run -t clean) and rebuild. This guards against the
    // silent stale-cache problem that wasted a debugging session.
    Serial.printf("[eDrum] Build stamp: %s %s\n", __DATE__, __TIME__);
    Serial.println("[LED] boot");

    configInit();
    configLoad();

    // Dev tooling (dev-build only). LittleFS is mounted by configInit() above, so
    // dev.txt can be read now. Order matters: parse dev.txt, then set the DevLog
    // serial-mirror from it, then apply file-controlled flags, then bring up WiFi.
    devcfg.begin();                                             // read /dev.txt
    DevLog.begin(devcfg.getBool("log_mirror_serial", true));    // seam config
    g_diagMode = devcfg.getBool("diag_mode", false);            // was hardcoded false
    devwifi.begin(devcfg);   // connect + start telnet (silent skip on fail/absent)

    // Layer 1 + 2: continuous DMA sampling -> ring/demux.
    if (!sampler.begin(kChannelGpios, 8, 8000)) {
        Serial.println("[ADC] ERROR: AdcSampler.begin() failed");
    }
    // Hold DMA sampling off while triggers are created/initialized and boot config
    // is applied below — same protection as the runtime g_apply_requested path
    // (buildDerived()'s ps_malloc/free churn across 4 engines could otherwise stall
    // long enough to overflow the store buffer before the pipeline is even fully
    // set up). Resumed just before the main loop starts sampling for real.
    sampler.pause();
    stream.begin(&sampler);
    Serial.printf("[ADC] configured %lu Hz/ch  (%lu Hz aggregate, %d ch)\n",
                  (unsigned long)sampler.sampleRateHz(),
                  (unsigned long)sampler.sampleRateHz() * sampler.numChannels(),
                  (int)sampler.numChannels());

    // Layer 3: one engine per input that has a head channel.
    for (int i = 0; i < NUM_INPUTS; i++) {
        if (kHeadCh[i] < 0) { triggers[i] = nullptr; continue; }
        triggers[i] = new PDrumTrigger(
            (byte)streamCh(kHeadCh[i]),
            (byte)streamCh(kRimCh[i]));
        triggers[i]->initialize(stream.sampleRateHz());
    }
    applyConfig();
    for (int i = 0; i < NUM_INPUTS; i++) {
        if (triggers[i]) triggers[i]->syncConfig();
    }
    sampler.resume();

    Serial.println("[LED] ready");
    printHelp();
}

void loop() {
    // Layer 2: pull all completed DMA frames into the ring buffer (advances head).
    stream.pump();

    // Dev telnet console: accept/maintain the client, route debug I/O (dev build
    // only; no-op otherwise). Independent of USB MIDI.
    devwifi.poll();

    // One-shot measured per-channel rate (driver-reported), printed ~1s after the
    // first sample so we can confirm the configured-vs-delivered rate at boot.
    static bool     s_rateDone = false;
    static bool     s_rateInit = false;
    static uint32_t s_rateT0   = 0;
    static uint32_t s_rateHead = 0;
    if (!s_rateDone) {
        if (!s_rateInit && stream.writeHead() > 0) {
            s_rateInit = true; s_rateT0 = millis(); s_rateHead = stream.writeHead();
        } else if (s_rateInit && millis() - s_rateT0 >= 1000) {
            DevLog.printf("[ADC] measured %lu Hz/ch (delivered)\n",
                          (unsigned long)(stream.writeHead() - s_rateHead));
            s_rateDone = true;
        }
    }

    // Status: log on USB host mount/unmount transitions
    static bool wasMounted = false;
    bool mounted = TinyUSBDevice.mounted();
    if (mounted != wasMounted) {
        wasMounted = mounted;
        DevLog.println(mounted ? "[LED] mounted" : "[LED] unmounted");
    }

    // Config apply / save each stall the loop long enough to overflow the ADC DMA
    // store buffer: applyConfig() marks the engines' decay LUTs dirty and syncConfig()
    // rebuilds them (ps_malloc/free + fill), and configSave() does a LittleFS write.
    // Pause DMA sampling across the whole block so a stall can't wedge the sampler,
    // and only pay one pause/resume cycle when both flags are set together.
    if (g_apply_requested || g_save_requested) {
        sampler.pause();

        if (g_apply_requested) {
            g_apply_requested = false;
            applyConfig();
            // Force each engine's deferred (needsInit_) rebuild NOW, inside the
            // pause bracket, instead of lazily on the next processBlock().
            for (int i = 0; i < NUM_INPUTS; i++) {
                if (triggers[i]) triggers[i]->syncConfig();
            }
        }

        if (g_save_requested) {
            g_save_requested = false;
            configSave();
            uint8_t ack[3] = {SYSEX_CAT_SYS, SYSEX_SYS_SAVE, SYSEX_ACK_OK};
            sysexSendResponse(SYSEX_DEV_HEAD, SYSEX_CAT_STATUS,
                            SYSEX_STAT_ACK, ack, 3);
        }

        sampler.resume();
    }

    // TEMP DIAGNOSTIC (DC-reseed runaway confirmation): print immediately whenever
    // an engine's seed count changes, independent of hit timing, so a reseed shows
    // up even if no hit follows right after. Should print exactly once per input,
    // at boot, and never again post-fix.
    static int s_lastSeedCount[NUM_INPUTS] = {0};
    for (int i = 0; i < NUM_INPUTS; i++) {
        if (!triggers[i]) continue;
        int sc = triggers[i]->getDebugSeedCount();
        if (sc != s_lastSeedCount[i]) {
            s_lastSeedCount[i] = sc;
            if (g_hitDebug && !g_adcDump) {
                DevLog.printf("[SEED] t=%lu i=%d count=%d raw=%.1f\n",
                              (unsigned long)millis(), i, sc, triggers[i]->getDebugLastSeedRaw());
            }
        }
    }

    MIDI.read();

    // Command input now arrives over the telnet console (serial RX is dead under
    // USB MIDI). DevLog reads from the telnet client; no-op if none connected.
    if (DevLog.available()) {
        handleSerial((char)DevLog.read());
    }

    static unsigned long lastAdcPrint = 0;
    if (g_adcDump && millis() - lastAdcPrint >= 100 && stream.writeHead() > 0) {
        lastAdcPrint = millis();
        uint32_t latest = stream.writeHead() - 1;
        uint16_t v[8] = {};
        for (int ch = 0; ch < 8; ch++) {
            stream.readWindow((uint8_t)ch, latest, &v[ch], 1);
        }
        // DIAGNOSTIC: print every interval unconditionally (no floor filter) so the
        // resting baseline is visible even when low. Columns are stream channels 0-7;
        // KD-80 head = jack 2 = GPIO6 = stream ch 4 (the 5th column).
        DevLog.printf("[ADC] %4d %4d %4d %4d %4d %4d %4d %4d\n",
            v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    }

    // Fire pending scope dump once 100 post-hit samples have accumulated.
    // SAFETY (Stage 1): all index math is unsigned, so guard against underflow and
    // stale/garbage snaps. Coordinate-accuracy is deferred to Stage 2 (processBlock
    // rewrite); here we only guarantee the dump can never fire on a bad index and
    // thus never flood/lock the serial link.
    if (g_scopePending) {
        uint32_t head = stream.writeHead();
        bool snapValid = (g_scopeSnap <= head)                       // not in the future
                      && (g_scopeSnap >= 100)                        // PRE window exists
                      && (head - g_scopeSnap <= SampleStream::kDepth);// still in ring
        if (!snapValid) {
            // Bad/stale snap — abandon quietly rather than dump garbage.
            g_scopePending = false;
        } else if ((head - g_scopeSnap) >= 100) {
            scopeDump((int)g_scopeInput, g_scopeIsRim);
            g_scopePending = false;
        }
    }

    // DIAGNOSTIC MODE: skip all detection + MIDI. Only the stream pump and the 'a'
    // ADC dump run, giving a quiet link to read the raw signal. Toggle with 'm'.
    if (g_diagMode) return;

    for (int i = 0; i < NUM_INPUTS; i++) {
        if (!triggers[i]) continue;
        if (!g_inputs[i].enabled) continue;   // input disabled: ignore entirely

        int hc = streamCh(kHeadCh[i]);
        int rc = streamCh(kRimCh[i]);
        if (hc < 0) continue;

        static uint16_t headBuf[kBlock];
        static uint16_t rimBuf[kBlock];

        // Capture the block's absolute start index BEFORE the read advances the cursor.
        uint32_t blockStartIdx = headCursor[i].pos;
        uint16_t n = stream.read((uint8_t)hc, headCursor[i], headBuf, kBlock);
        if (rc >= 0) stream.read((uint8_t)rc, rimCursor[i], rimBuf, n);  // same count
        if (n == 0) continue;

        // If the read reset the cursor (overrun), the actual block start is pos - n.
        blockStartIdx = headCursor[i].pos - n;

        if (headCursor[i].overran || (rc >= 0 && rimCursor[i].overran)) {
            if (!g_serialQuiet) DevLog.printf("[WARN] i=%d sample overrun (consumer fell behind)\n", i);
        }

        triggers[i]->processBlock(headBuf, rc >= 0 ? rimBuf : nullptr, n, blockStartIdx);

        // Absolute stream index of the last head sample in this block.
        uint32_t blockEndIdx = headCursor[i].pos - 1;

        if (triggers[i]->hasHit()) {
            byte note    = triggers[i]->getNoteHead();
            byte vel     = (byte)constrain(triggers[i]->getVelocity(), 0, 127);
            byte ch      = g_inputs[i].midiChannel;
            byte raw_vel = rawToMidi(triggers[i]->getVelocityRaw(),
                                     g_inputs[i].threshold,
                                     g_inputs[i].headSensitivity);
            MIDI.sendNoteOn(note, vel, ch);
            MIDI.sendNoteOff(note, 0, ch);
            if (g_hitDebug && !g_adcDump) DevLog.printf("[HIT] t=%lu i=%d note=%d vel=%d raw=%d truepeak=%d peakidx=%d ch=%d rescues=%d thresh=%.3f peak=%.3f mask=%d decay=%d decaylen=%d xfilt=%.3f xfiltdecay=%.3f loopt=%d piezodc=%.1f dchead=%.1f spikerej=%d retrigrej=%d\n",
                         (unsigned long)millis(), i, note, vel, raw_vel,
                         triggers[i]->getVelocityRaw(), triggers[i]->getLastPeakIdx(), ch,
                         triggers[i]->getRescueCount(), triggers[i]->getDebugThreshold(),
                         triggers[i]->getDebugPeakVal(), triggers[i]->getDebugMaskCnt(),
                         triggers[i]->getDebugDecayCnt(), triggers[i]->getDebugDecayLen(),
                         triggers[i]->getDebugXFilt(), triggers[i]->getDebugXFiltDecay(),
                         triggers[i]->getDebugLoopTimes(), triggers[i]->getDebugPiezoAfterDc(),
                         triggers[i]->getDebugDcOffsetHead(), triggers[i]->getDebugSpikeRejects(),
                         triggers[i]->getDebugRetriggerRejects());
            // 05 03 — 4 bytes: input_id, zone, raw_vel, midi_vel.
            // Sent unconditionally: the config app's hit log depends on this. (Only
            // the noisy serial [HIT] print above is gated; the SysEx event is a
            // product feature, not debug spam.)
            {
                uint8_t dbg[4] = { (uint8_t)i, SYSEX_ZONE_HEAD, raw_vel, vel };
                sysexSendResponse(SYSEX_DEV_HEAD, SYSEX_CAT_STATUS,
                                  SYSEX_STAT_HIT_DEBUG, dbg, 4);
            }
            if (g_scopeActive && !g_adcDump && i == (int)g_scopeInput && !g_scopePending
                    && (triggers[i]->getVelocityRaw()    >= g_scopeFloor
                     || triggers[i]->getVelocityRimRaw() >= g_scopeFloor)) {
                // SAFETY: only arm if the snap subtraction won't underflow.
                uint32_t back = triggers[i]->getTriggerSnap();
                if (back <= blockEndIdx) {
                    g_scopePending = true;
                    g_scopeSnap    = blockEndIdx - back;
                    g_scopeIsRim   = false;
                }
            }
        } else if (triggers[i]->hasHitRim()) {
            byte note    = g_inputs[i].zone2MidiNote;
            byte vel     = (byte)constrain(triggers[i]->getVelocityRim(), 0, 127);
            byte ch      = g_inputs[i].zone2MidiChannel;
            byte raw_vel = rawToMidi(triggers[i]->getVelocityRimRaw(),
                                     g_inputs[i].threshold,
                                     g_inputs[i].headSensitivity);
            MIDI.sendNoteOn(note, vel, ch);
            MIDI.sendNoteOff(note, 0, ch);
            if (g_hitDebug && !g_adcDump) DevLog.printf("[RIM] t=%lu i=%d note=%d vel=%d raw=%d truepeak=%d peakidx=%d ch=%d rescues=%d thresh=%.3f peak=%.3f mask=%d decay=%d decaylen=%d xfilt=%.3f xfiltdecay=%.3f loopt=%d piezodc=%.1f dchead=%.1f spikerej=%d retrigrej=%d\n",
                         (unsigned long)millis(), i, note, vel, raw_vel,
                         triggers[i]->getVelocityRimRaw(), triggers[i]->getLastPeakIdx(), ch,
                         triggers[i]->getRescueCount(), triggers[i]->getDebugThreshold(),
                         triggers[i]->getDebugPeakVal(), triggers[i]->getDebugMaskCnt(),
                         triggers[i]->getDebugDecayCnt(), triggers[i]->getDebugDecayLen(),
                         triggers[i]->getDebugXFilt(), triggers[i]->getDebugXFiltDecay(),
                         triggers[i]->getDebugLoopTimes(), triggers[i]->getDebugPiezoAfterDc(),
                         triggers[i]->getDebugDcOffsetHead(), triggers[i]->getDebugSpikeRejects(),
                         triggers[i]->getDebugRetriggerRejects());
            // 05 03 — 4 bytes: input_id, zone, raw_vel, midi_vel (app hit log depends on this)
            {
                uint8_t dbg[4] = { (uint8_t)i, SYSEX_ZONE_RIM, raw_vel, vel };
                sysexSendResponse(SYSEX_DEV_HEAD, SYSEX_CAT_STATUS,
                                  SYSEX_STAT_HIT_DEBUG, dbg, 4);
            }
            if (g_scopeActive && !g_adcDump && i == (int)g_scopeInput && !g_scopePending
                    && (triggers[i]->getVelocityRaw()    >= g_scopeFloor
                     || triggers[i]->getVelocityRimRaw() >= g_scopeFloor)) {
                // SAFETY: only arm if the snap subtraction won't underflow.
                uint32_t back = triggers[i]->getTriggerSnap();
                if (back <= blockEndIdx) {
                    g_scopePending = true;
                    g_scopeSnap    = blockEndIdx - back;
                    g_scopeIsRim   = true;
                }
            }
        }

        // Choke — PIEZO_SWITCH_CHOKE pads only
        if (triggers[i]->hasChoke()) {
            triggers[i]->clearChoke();
            byte note = g_inputs[i].midiNote;
            byte ch   = g_inputs[i].midiChannel;
            MIDI.sendNoteOff(note, 0, ch);
            if (!g_serialQuiet) DevLog.printf("[CHOKE] i=%d note=%d ch=%d\n", i, note, ch);
        }

        // TEMP DIAGNOSTIC (retrigger-cancel validation): a rejected hit produces no
        // MIDI/hit event at all, so this is the only visibility into what actually
        // got rejected and why (peak-timing detail, not just a running count).
        if (g_hitDebug && !g_adcDump && triggers[i]->hasReject()) {
            DevLog.printf("[REJECT] t=%lu i=%d peakidx=%d velraw=%d retrig_cutoff=%d\n",
                          (unsigned long)millis(), i,
                          triggers[i]->getLastRejectPeakIdx(),
                          triggers[i]->getLastRejectVelocityRaw(),
                          (int)g_inputs[i].retriggerTime);
            triggers[i]->clearReject();
        }
    }
}
