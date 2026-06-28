// XIAO ESP32-S3 dual-core eDrum firmware (head unit)
// Hardware-validation port of main_rp2040.cpp:
//   - internal ESP32-S3 ADC (analogRead) replaces the external MCP3008
//   - NeoPixel status LED replaced with Serial log messages
//   - RP2040 bootrom reset replaced with ESP.restart()
// The sensing layer is accessed only through the TriggerEngine interface.

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
#include "sensing/pdrum/PDrumTrigger.h"
#include "ring_buffer.h"

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
// Config apply/save request flags (set by SysEx handlers, serviced on Core 0)
// ---------------------------------------------------------------------------

volatile bool g_save_requested  = false;
volatile bool g_apply_requested = false;

// ---------------------------------------------------------------------------
// ADC — owned by Core 1
// ---------------------------------------------------------------------------

#define ADC_PRINT_FLOOR 10

// ---------------------------------------------------------------------------
// Ring buffer storage (declared in ring_buffer.h)
// ---------------------------------------------------------------------------

uint16_t          ringBuf[8][RING_BUF_SIZE];
volatile uint32_t ringHead = 0;

// ---------------------------------------------------------------------------
// Trigger engine instances
// ---------------------------------------------------------------------------

static TriggerEngine* triggers[NUM_INPUTS];

// ADC channel per input: {headCh, rimCh}; -1 = no ADC channel (stub)
// ESP32-S3 GPIO numbers used directly with analogRead().
// Jacks 0-3: head/piezo + rim; Jack 4: hi-hat controller — stubbed, no channel.
static const int8_t kHeadCh[NUM_INPUTS] = { 2, 4, 6, 8, -1 };
static const int8_t kRimCh[NUM_INPUTS]  = { 3, 5, 7, 9, -1 };

static void applyConfig() {
    for (int i = 0; i < NUM_INPUTS; i++) {
        if (!triggers[i]) continue;
        triggers[i]->setPadType(g_inputs[i].padType);
        triggers[i]->setRimRatioThreshold(g_inputs[i].rimRatioThreshold);
        triggers[i]->setChokeThreshold(g_inputs[i].chokeThreshold);
        triggers[i]->setChokeEnabled(g_inputs[i].chokeEnabled);
        triggers[i]->setHeadThreshold(g_inputs[i].threshold);
        triggers[i]->setHeadSensitivity(g_inputs[i].headSensitivity);
        triggers[i]->setScanTime(g_inputs[i].scanTime);
        triggers[i]->setMaskTime(g_inputs[i].maskTime);
        triggers[i]->setCurveType(g_inputs[i].velocityCurve);
        triggers[i]->setNoteHead(g_inputs[i].midiNote);
    }
}

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
    Serial.printf("[eDrum] Build %d — p=ping  i=identify  s=config  n=test note  a=toggle ADC dump\n", FW_BUILD);
    Serial.println("  o <input> <floor> = scope input (e.g. o 0 10)   o off = disable scope");
    Serial.println("  w <input> <param> <value> = set param (e.g. w 0 scan 3)");
    Serial.println("  params: thresh sens scan mask retrig type ratio chokethresh choke");
}

static bool g_adcDump = false;
bool g_serialQuiet = false;

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
            Serial.println("[>] SysEx ping sent");
            break;
        }
        case 'i': {
            uint8_t msg[] = { 0xF0, SYSEX_MFR_0, SYSEX_MFR_1, SYSEX_DEV_HEAD,
                              SYSEX_CAT_SYS, SYSEX_SYS_IDENT_REQ, 0xF7 };
            usbMidiSendSysEx(msg, sizeof(msg));
            Serial.println("[>] SysEx identify sent");
            break;
        }
        case 's': {
            Serial.println("[Config]");
            for (int i = 0; i < NUM_INPUTS; i++) {
                Serial.printf("  [%d] type=%d note=%d ch=%d z2note=%d z2ch=%d"
              " thresh=%d sens=%d scan=%d mask=%d"
              " ratio=%d chokethresh=%d choke=%d curve=%d retrig=%d\n",
                    i,
                    g_inputs[i].padType,
                    g_inputs[i].midiNote,    g_inputs[i].midiChannel,
                    g_inputs[i].zone2MidiNote, g_inputs[i].zone2MidiChannel,
                    g_inputs[i].threshold,   g_inputs[i].headSensitivity,
                    g_inputs[i].scanTime,    g_inputs[i].maskTime,
                    g_inputs[i].rimRatioThreshold, g_inputs[i].chokeThreshold,
                    (int)g_inputs[i].chokeEnabled,
                    g_inputs[i].velocityCurve, g_inputs[i].retriggerTime);
            }
            break;
        }
        case 'n': {
            // C3 = MIDI note 48
            MIDI.sendNoteOn(48, 100, 10);
            MIDI.sendNoteOff(48, 0, 10);
            Serial.println("[>] Note C3 vel=100 ch=10");
            break;
        }
        case 'r': {
            Serial.println("[eDrum] Restarting...");
            delay(100);
            ESP.restart();
            break;
        }
        case 'a': {
            g_adcDump = !g_adcDump;
            if (g_adcDump && g_scopeActive) {
                g_scopeActive  = false;
                g_scopePending = false;
                Serial.println("[SCOPE] Warning: scope disabled — ADC dump active");
            }
            g_serialQuiet = g_adcDump;
            Serial.println(g_adcDump ? "[ADC] Dump ON" : "[ADC] Dump OFF");
            break;
        }
        case 'o': {
            String args = Serial.readStringUntil('\n');
            args.trim();
            if (args.length() == 0) {
                Serial.println("[SCOPE] Usage: o <input> <floor>  |  o off");
            } else if (args.startsWith("off")) {
                g_scopeActive  = false;
                g_scopePending = false;
                Serial.println("[SCOPE] Disabled");
            } else {
                int inp = -1, flr = 10;
                if (sscanf(args.c_str(), "%d %d", &inp, &flr) >= 1
                        && inp >= 0 && inp < NUM_INPUTS) {
                    g_scopeActive  = true;
                    g_scopeInput   = (uint8_t)inp;
                    g_scopeFloor   = (uint16_t)flr;
                    g_scopePending = false;
                    Serial.printf("[SCOPE] Active: input=%d floor=%d\n", inp, flr);
                } else {
                    Serial.printf("[SCOPE] Error: input must be 0-%d\n", NUM_INPUTS - 1);
                }
            }
            break;
        }
        case 'h': {
            printHelp();
            break;
        }
        case 'w': {
            String args = Serial.readStringUntil('\n');
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
                else { Serial.printf("[w] Unknown param '%s'\n", param); ok = false; }
                if (ok) {
                    applyConfig();
                    g_save_requested = true;
                    Serial.printf("[w] input=%d %s=%d OK\n", inp, param, val);
                }
            } else {
                Serial.println("[w] Usage: w <input> <param> <value>");
                Serial.println("[w] params: thresh sens scan mask retrig type ratio chokethresh choke");
            }
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Helper: map raw ADC value to 0-127, mirroring pdrum.cpp curve() map()
// ---------------------------------------------------------------------------

static uint8_t rawToMidi(int raw, uint16_t threshold, uint16_t sens) {
    if (raw <= (int)threshold) return 0;
    long mapped = map((long)raw, (long)threshold, (long)sens, 1, 127);
    if (mapped < 0)   return 0;
    if (mapped > 127) return 127;
    return (uint8_t)mapped;
}

// ---------------------------------------------------------------------------
// Scope capture dump — called after 100 post-hit samples have accumulated
// ---------------------------------------------------------------------------

static void scopeDump(int input, bool isRim) {
    int8_t hc = kHeadCh[input];
    int8_t rc = kRimCh[input];

    static const uint32_t PRE   = 100;
    static const uint32_t POST  = 100;
    static const uint32_t TOTAL = PRE + POST;

    int headPeak = 0, rimPeak = 0;
    for (uint32_t t = 0; t < TOTAL; t++) {
        uint32_t idx = g_scopeSnap - PRE + t;
        if (hc >= 0) { int v = (int)ringBufRead((uint8_t)hc, idx); if (v > headPeak) headPeak = v; }
        if (rc >= 0) { int v = (int)ringBufRead((uint8_t)rc, idx); if (v > rimPeak)  rimPeak  = v; }
    }

    Serial.printf("[SCOPE] input=%d pad_type=%d head_ch=%d rim_ch=%d head_peak=%d rim_peak=%d decision=%s samples=%d\n",
        input, (int)g_inputs[input].padType, (int)hc, (int)rc, headPeak, rimPeak, isRim ? "RIM" : "HEAD", (int)TOTAL);
    Serial.println("T,H,R");
    for (uint32_t t = 0; t < TOTAL; t++) {
        uint32_t idx = g_scopeSnap - PRE + t;
        int h = (hc >= 0) ? (int)ringBufRead((uint8_t)hc, idx) : 0;
        int r = (rc >= 0) ? (int)ringBufRead((uint8_t)rc, idx) : 0;
        Serial.printf("%d,%d,%d\n", (int)t, h, r);
    }
}

// ---------------------------------------------------------------------------
// Core 1 — ADC sampling task (pinned to Core 1, launched from setup())
// ---------------------------------------------------------------------------

// ADC sampling — runs inline in loop() on Core 0.
// analogRead() is not safe to call from Core 1 on ESP32-S3 (the Arduino ADC
// driver assumes Core 0). For this hardware-validation pass we sample on
// Core 0 before the sensing loop. DMA continuous sampling (Core 1) comes
// with the PDrum2 sensing rewrite.
static void sampleADC() {
    uint16_t samples[8];
    samples[0] = (uint16_t)analogRead(2);
    samples[1] = (uint16_t)analogRead(3);
    samples[2] = (uint16_t)analogRead(4);
    samples[3] = (uint16_t)analogRead(5);
    samples[4] = (uint16_t)analogRead(6);
    samples[5] = (uint16_t)analogRead(7);
    samples[6] = (uint16_t)analogRead(8);
    samples[7] = (uint16_t)analogRead(9);
    ringBufWrite(samples);
}

// ---------------------------------------------------------------------------
// Core 0 — USB MIDI, config, sensing, output
// ---------------------------------------------------------------------------

void setup() {
    if (!TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.begin(0);
    }
    usb_midi.setStringDescriptor("eDrum");
    MIDI.begin(MIDI_CHANNEL_OMNI);
    MIDI.setHandleSystemExclusive(onSysEx);
    Serial.begin(115200);
    delay(2000);
    Serial.println("[eDrum] Ready.");

    Serial.println("[LED] boot");

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    ringBufInit();

    configInit();
    configLoad();

    for (int i = 0; i < NUM_INPUTS; i++) {
        int8_t h = kHeadCh[i];
        int8_t r = kRimCh[i];
        triggers[i] = new PDrumTrigger(
            (r >= 0) ? (byte)r : (byte)0,
            (h >= 0) ? (byte)h : (byte)0
        );
    }
    applyConfig();

    Serial.println("[LED] ready");
    printHelp();
}

void loop() {
    // Sample ADC on Core 0 (analogRead not safe on Core 1)
    sampleADC();

    // Status: log on USB host mount/unmount transitions
    static bool wasMounted = false;
    bool mounted = TinyUSBDevice.mounted();
    if (mounted != wasMounted) {
        wasMounted = mounted;
        Serial.println(mounted ? "[LED] mounted" : "[LED] unmounted");
    }

    if (g_apply_requested) {
        g_apply_requested = false;
        applyConfig();
    }

    if (g_save_requested) {
        g_save_requested = false;
        configSave();
        uint8_t ack[3] = {SYSEX_CAT_SYS, SYSEX_SYS_SAVE, SYSEX_ACK_OK};
        sysexSendResponse(SYSEX_DEV_HEAD, SYSEX_CAT_STATUS,
                        SYSEX_STAT_ACK, ack, 3);
    }


    // --- TEMP sample-rate measurement — remove after characterising ---
    static unsigned long lastRatePrint = 0;
    static uint32_t      lastRingHead  = 0;
    if (millis() - lastRatePrint >= 1000) {
        uint32_t now = ringHead;
        uint32_t delta = now - lastRingHead;   // loops completed in ~1s
        lastRingHead  = now;
        lastRatePrint = millis();
        Serial.printf("[RATE] %lu samples/sec/channel\n", (unsigned long)delta);
    }


    MIDI.read();

    if (Serial.available()) {
        handleSerial((char)Serial.read());
    }

    static unsigned long lastAdcPrint = 0;
    if (g_adcDump && millis() - lastAdcPrint >= 100) {
        lastAdcPrint = millis();
        uint32_t snap = ringHead;
        bool anyAboveFloor = false;
        for (int ch = 0; ch < 8; ch++) {
            if (ringBufRead((uint8_t)ch, snap - 1) > ADC_PRINT_FLOOR) {
                anyAboveFloor = true;
                break;
            }
        }
        if (anyAboveFloor) {
            Serial.printf("[ADC] %4d %4d %4d %4d %4d %4d %4d %4d\n",
                ringBufRead(0, snap-1), ringBufRead(1, snap-1),
                ringBufRead(2, snap-1), ringBufRead(3, snap-1),
                ringBufRead(4, snap-1), ringBufRead(5, snap-1),
                ringBufRead(6, snap-1), ringBufRead(7, snap-1));
        }
    }

    // Fire pending scope dump once 100 post-hit samples have accumulated
    if (g_scopePending && (ringHead - g_scopeSnap) >= 100) {
        scopeDump((int)g_scopeInput, g_scopeIsRim);
        g_scopePending = false;
    }

    for (int i = 0; i < NUM_INPUTS; i++) {
        if (!triggers[i]) continue;

        int8_t hc = kHeadCh[i];
        int8_t rc = kRimCh[i];
        int headVal = (hc >= 0) ? (int)ringBufRead((uint8_t)hc, ringHead - 1) : 0;
        int rimVal  = (rc >= 0) ? (int)ringBufRead((uint8_t)rc, ringHead - 1) : 0;

        triggers[i]->sensing(headVal, rimVal, ringHead);

        if (triggers[i]->hasHit()) {
            byte note    = triggers[i]->getNoteHead();
            byte vel     = (byte)constrain(triggers[i]->getVelocity(), 0, 127);
            byte ch      = g_inputs[i].midiChannel;
            byte raw_vel = rawToMidi(triggers[i]->getVelocityRaw(),
                                     g_inputs[i].threshold,
                                     g_inputs[i].headSensitivity);
            MIDI.sendNoteOn(note, vel, ch);
            MIDI.sendNoteOff(note, 0, ch);
            if (!g_adcDump) Serial.printf("[HIT] i=%d note=%d vel=%d raw=%d ch=%d\n",
                         i, note, vel, raw_vel, ch);
            // 05 03 — 4 bytes: input_id, zone, raw_vel, midi_vel
            uint8_t dbg[4] = { (uint8_t)i, SYSEX_ZONE_HEAD, raw_vel, vel };
            sysexSendResponse(SYSEX_DEV_HEAD, SYSEX_CAT_STATUS,
                              SYSEX_STAT_HIT_DEBUG, dbg, 4);
            if (g_scopeActive && !g_adcDump && i == (int)g_scopeInput && !g_scopePending
                    && (triggers[i]->getVelocityRaw()    >= g_scopeFloor
                     || triggers[i]->getVelocityRimRaw() >= g_scopeFloor)) {
                g_scopePending = true;
                g_scopeSnap    = triggers[i]->getTriggerSnap();
                g_scopeIsRim   = false;
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
            if (!g_adcDump) Serial.printf("[RIM] i=%d note=%d vel=%d raw=%d ch=%d\n",
                         i, note, vel, raw_vel, ch);
            // 05 03 — 4 bytes: input_id, zone, raw_vel, midi_vel
            uint8_t dbg[4] = { (uint8_t)i, SYSEX_ZONE_RIM, raw_vel, vel };
            sysexSendResponse(SYSEX_DEV_HEAD, SYSEX_CAT_STATUS,
                              SYSEX_STAT_HIT_DEBUG, dbg, 4);
            if (g_scopeActive && !g_adcDump && i == (int)g_scopeInput && !g_scopePending
                    && (triggers[i]->getVelocityRaw()    >= g_scopeFloor
                     || triggers[i]->getVelocityRimRaw() >= g_scopeFloor)) {
                g_scopePending = true;
                g_scopeSnap    = triggers[i]->getTriggerSnap();
                g_scopeIsRim   = true;
            }
        }

        // Choke — PIEZO_SWITCH_CHOKE pads only
        if (triggers[i]->hasChoke()) {
            triggers[i]->clearChoke();
            byte note = g_inputs[i].midiNote;
            byte ch   = g_inputs[i].midiChannel;
            MIDI.sendNoteOff(note, 0, ch);
            if (!g_serialQuiet) Serial.printf("[CHOKE] i=%d note=%d ch=%d\n", i, note, ch);
        }
    }
}
