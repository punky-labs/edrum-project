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
        drums[i]->noteHead      = g_inputs[i].midiNote;
        drums[i]->noteRim       = g_inputs[i].zone2MidiNote;
        drums[i]->headThreshold = g_inputs[i].threshold;
        drums[i]->rimThreshold  = g_inputs[i].threshold;
        drums[i]->curvetype     = g_inputs[i].velocityCurve;
        drums[i]->masktime      = g_inputs[i].retriggerTime;
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
    sysexParse(data, (size_t)size);
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
            byte note = drums[i]->noteHead;
            byte vel  = (byte)constrain(drums[i]->velocity, 0, 127);
            byte ch   = g_inputs[i].midiChannel;
            MIDI.sendNoteOn(note, vel, ch);
            MIDI.sendNoteOff(note, 0, ch);
            Serial.printf("[HIT] i=%d note=%d vel=%d ch=%d\n", i, note, vel, ch);
        } else if (drums[i]->hitRim) {
            byte note = drums[i]->noteRim;
            byte vel  = (byte)constrain(drums[i]->velocity, 0, 127);
            byte ch   = g_inputs[i].zone2MidiChannel;
            MIDI.sendNoteOn(note, vel, ch);
            MIDI.sendNoteOff(note, 0, ch);
            Serial.printf("[RIM] i=%d note=%d vel=%d ch=%d\n", i, note, vel, ch);
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
