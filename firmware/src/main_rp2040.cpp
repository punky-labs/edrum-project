// RP2040 dual-core eDrum firmware

#include <Arduino.h>
#include <Adafruit_MCP3008.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <MIDI.h>
#include <LittleFS.h>

#include <hardware/resets.h>
#include <pico/bootrom.h>

#include "config/Config.h"
#include "midi/SysEx.h"
#include "pdrum/pdrum.h"

// ---------------------------------------------------------------------------
// USB MIDI
// ---------------------------------------------------------------------------

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// ---------------------------------------------------------------------------
// NeoPixel LED
// ---------------------------------------------------------------------------

#define PIN_PWR   11
#define NUMPIXELS 1

volatile bool g_save_requested = false;

Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

enum LEDColor {
    BLACK, RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE, ORANGE, PURPLE, PINK
};


static void setLED(LEDColor c) {
    uint32_t rgb;
    switch (c) {
        case RED:     rgb = pixels.Color(255, 0,   0);   break;
        case GREEN:   rgb = pixels.Color(0,   255, 0);   break;
        case BLUE:    rgb = pixels.Color(0,   0,   255); break;
        case YELLOW:  rgb = pixels.Color(255, 255, 0);   break;
        case CYAN:    rgb = pixels.Color(0,   255, 255); break;
        case MAGENTA: rgb = pixels.Color(255, 0,   255); break;
        case WHITE:   rgb = pixels.Color(255, 255, 255); break;
        case ORANGE:  rgb = pixels.Color(255, 165, 0);   break;
        case PURPLE:  rgb = pixels.Color(128, 0,   128); break;
        case PINK:    rgb = pixels.Color(255, 192, 203); break;
        default:      rgb = pixels.Color(0,   0,   0);   break;
    }
    pixels.setPixelColor(0, rgb);
    pixels.show();
}

// ---------------------------------------------------------------------------
// ADC — owned by Core 1
// ---------------------------------------------------------------------------

Adafruit_MCP3008 adc;
volatile int pinVals[8];

#define SMOOTHING 0.1f
#define FLOORVAL  0

static int smoothVal(int prev, int reading) {
    if (reading < FLOORVAL) reading = 0;
    return (int)(SMOOTHING * (float)prev + (1.0f - SMOOTHING) * (float)reading);
}

// ---------------------------------------------------------------------------
// PDrum instances
// ---------------------------------------------------------------------------

static PDrum* drums[NUM_INPUTS];

// ADC channel per input: {headCh, rimCh}; -1 = no ADC channel (stub)
// Inputs 0-3: dual zone  head = idx*2+1, rim = idx*2
// Input  4:   hihat      channel 8 exceeds MCP3008 range → stub for now
// Inputs 5-8: single-channel stubs
static const int8_t kHeadCh[NUM_INPUTS] = { 1, 3, 5, 7, -1, -1, -1, -1, -1 };
static const int8_t kRimCh[NUM_INPUTS]  = { 0, 2, 4, 6, -1, -1, -1, -1, -1 };

static void applyConfig() {
    for (int i = 0; i < NUM_INPUTS; i++) {
        if (!drums[i]) continue;
        drums[i]->noteHead        = g_inputs[i].midiNote;
        drums[i]->noteRim         = g_inputs[i].zone2MidiNote;
        drums[i]->headThreshold   = g_inputs[i].threshold;
        drums[i]->headSensitivity = g_inputs[i].headSensitivity;
        drums[i]->scantime        = g_inputs[i].scanTime;
        drums[i]->masktime        = g_inputs[i].maskTime;
        drums[i]->rimThreshold    = g_inputs[i].rimThreshold;
        drums[i]->rimSensitivity  = g_inputs[i].rimSensitivity;
        drums[i]->curvetype       = g_inputs[i].velocityCurve;
    }
}

// ---------------------------------------------------------------------------
// SysEx USB bridge
// BleMidi.cpp is excluded from xiao_rp2040 build; provide stubs here so that
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
    Serial.println("[eDrum] p=ping  i=identify  s=config  n=test note (C3 ch10)");
}

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
                Serial.printf("  [%d] note=%d ch=%d z2note=%d z2ch=%d"
                              " thresh=%d curve=%d retrig=%d\n",
                    i,
                    g_inputs[i].midiNote,    g_inputs[i].midiChannel,
                    g_inputs[i].zone2MidiNote, g_inputs[i].zone2MidiChannel,
                    g_inputs[i].threshold,   g_inputs[i].velocityCurve,
                    g_inputs[i].retriggerTime);
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
            Serial.println("[eDrum] Rebooting to bootloader...");
            delay(100);
            reset_usb_boot(0, 0);
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

    pixels.begin();
    pinMode(PIN_PWR, OUTPUT);
    digitalWrite(PIN_PWR, HIGH);
    setLED(ORANGE);

    configInit();
    configLoad();

    for (int i = 0; i < NUM_INPUTS; i++) {
        int8_t h = kHeadCh[i];
        int8_t r = kRimCh[i];
        drums[i] = new PDrum(
            (r >= 0) ? (byte)r : (byte)0,
            (h >= 0) ? (byte)h : (byte)0
        );
    }
    applyConfig();

    setLED(BLUE);
    Serial.println("[eDrum] Ready.");
    printHelp();
}

void loop() {
    // LED: green while USB host is connected, blue otherwise
    static bool wasMounted = false;
    bool mounted = TinyUSBDevice.mounted();
    if (mounted != wasMounted) {
        wasMounted = mounted;
        setLED(mounted ? GREEN : BLUE);
    }

    if (g_save_requested) {
        g_save_requested = false;
        configSave();
        uint8_t ack[3] = {SYSEX_CAT_SYS, SYSEX_SYS_SAVE, SYSEX_ACK_OK};
        sysexSendResponse(SYSEX_DEV_HEAD, SYSEX_CAT_STATUS,
                        SYSEX_STAT_ACK, ack, 3);
    }

    MIDI.read();

    if (Serial.available()) {
        handleSerial((char)Serial.read());
    }

    for (int i = 0; i < NUM_INPUTS; i++) {
        if (!drums[i]) continue;

        int8_t hc = kHeadCh[i];
        int8_t rc = kRimCh[i];
        int headVal = (hc >= 0) ? (int)pinVals[hc] : 0;
        int rimVal  = (rc >= 0) ? (int)pinVals[rc]  : 0;

        drums[i]->sensing(headVal, rimVal);

        if (drums[i]->hit) {
            byte note    = drums[i]->noteHead;
            byte vel     = (byte)constrain(drums[i]->velocity, 0, 127);
            byte ch      = g_inputs[i].midiChannel;
            byte raw_vel = rawToMidi(drums[i]->velocityRaw,
                                     g_inputs[i].threshold,
                                     g_inputs[i].headSensitivity);
            MIDI.sendNoteOn(note, vel, ch);
            MIDI.sendNoteOff(note, 0, ch);
            Serial.printf("[HIT] i=%d note=%d vel=%d raw=%d ch=%d\n",
                          i, note, vel, raw_vel, ch);
            // 05 03 — 4 bytes: input_id, zone, raw_vel, midi_vel
            uint8_t dbg[4] = { (uint8_t)i, SYSEX_ZONE_HEAD, raw_vel, vel };
            sysexSendResponse(SYSEX_DEV_HEAD, SYSEX_CAT_STATUS,
                              SYSEX_STAT_HIT_DEBUG, dbg, 4);
        } else if (drums[i]->hitRim) {
            byte note    = drums[i]->noteRim;
            byte vel     = (byte)constrain(drums[i]->velocityRim, 0, 127);
            byte ch      = g_inputs[i].zone2MidiChannel;
            byte raw_vel = rawToMidi(drums[i]->velocityRimRaw,
                                     g_inputs[i].rimThreshold,
                                     g_inputs[i].rimSensitivity);
            MIDI.sendNoteOn(note, vel, ch);
            MIDI.sendNoteOff(note, 0, ch);
            Serial.printf("[RIM] i=%d note=%d vel=%d raw=%d ch=%d\n",
                          i, note, vel, raw_vel, ch);
            // 05 03 — 4 bytes: input_id, zone, raw_vel, midi_vel
            uint8_t dbg[4] = { (uint8_t)i, SYSEX_ZONE_RIM, raw_vel, vel };
            sysexSendResponse(SYSEX_DEV_HEAD, SYSEX_CAT_STATUS,
                              SYSEX_STAT_HIT_DEBUG, dbg, 4);
        }
    }
}

// ---------------------------------------------------------------------------
// Core 1 — ADC sampling, runs as fast as possible
// ---------------------------------------------------------------------------

void setup1() {
    // SCK=2, MOSI=3, MISO=4, CS=1
    adc.begin(2, 3, 4, 1);
}

void loop1() {
    for (int ch = 0; ch < 8; ch++) {
        pinVals[ch] = smoothVal((int)pinVals[ch], adc.readADC(ch));
    }
}
