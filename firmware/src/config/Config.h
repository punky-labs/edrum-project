#pragma once
#include <Arduino.h>
#include <LittleFS.h>

#define NUM_INPUTS      5
#define MAX_PRESETS     16
#define PRESET_NAME_LEN 16

extern bool g_serialQuiet;

struct __attribute__((packed)) InputConfig {
    uint8_t  padType;
    uint16_t threshold;      // 0–1023 (ADC range); encode as 2x 7-bit bytes in SysEx
    uint8_t  velocityCurve;
    uint16_t retriggerTime;  // ms; encode as 2x 7-bit bytes in SysEx
    uint16_t headSensitivity;  // upper ADC bound for velocity scaling; default 1000
    uint16_t scanTime;         // peak scan window ms; default 10
    uint16_t maskTime;         // post-hit ignore window ms; default 30
    uint16_t rimRatioThreshold;  // DUAL_PIEZO: ratio*100 threshold (e.g. 40 = 0.40 ratio)
    uint16_t chokeThreshold;     // PIEZO_SWITCH_CHOKE: ADC units for switch detection
    bool     chokeEnabled;       // PIEZO_SWITCH_CHOKE: enable choke detection
    uint8_t  crosstalkGroup;
    uint8_t  midiNote;
    uint8_t  midiChannel;
    uint8_t  zone2MidiNote;
    uint8_t  zone2MidiChannel;
    uint8_t  ccNumber;
    uint8_t  ccChannel;
    uint8_t  linkedInput;    // 0xFF = no link, 0x00–0x08 = paired input ID
};

struct __attribute__((packed)) Preset {
    char        name[PRESET_NAME_LEN + 1];
    InputConfig inputs[NUM_INPUTS];
};

extern InputConfig g_inputs[NUM_INPUTS];

void configInit();   // must be called once in setup() before configLoad()
void configLoad();
void configSave();
void configResetDefaults();

bool presetLoad(uint8_t id);
bool presetRead(uint8_t id, Preset* out);
bool presetSave(uint8_t id, const char* name);
bool presetDelete(uint8_t id);
