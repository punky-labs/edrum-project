#include "Config.h"
#include <LittleFS.h>
#include <string.h>
#include <stdio.h>

InputConfig g_inputs[NUM_INPUTS];

static const char* CFG_FILE = "/config.bin";

static void presetPath(uint8_t id, char* buf) {
    snprintf(buf, 16, "/preset_%X.bin", id & 0x0F);
}

static InputConfig defaultInput(uint8_t idx) {
    InputConfig c = {};
    c.linkedInput      = 0xFF;
    c.enabled          = true;  // inputs active by default; disable unpopulated jacks
    c.padType          = 1;    // PIEZO_SWITCH_CHOKE (safest default)
    // Stage 2a: threshold/headSensitivity now carry Edrumulus velocity_threshold/
    // velocity_sensitivity (0..31), NOT ADC units. KD8-derived defaults below.
    c.threshold        = 8;    // velocity_threshold (0..31), KD8 global
    c.velocityCurve    = 4;    // LOG2 (kick: less dynamic), KD8
    c.retriggerTime    = 50;
    c.headSensitivity  = 2;    // velocity_sensitivity (0..31), KD8
    c.scanTime         = 3;    // scan_time_ms (KD8 = 3.0)
    c.maskTime         = 6;    // mask_time_ms (global = 6)
    c.rimRatioThreshold = 40;  // ratio*100: rim/head > 0.40 = rim hit
    c.chokeThreshold   = 50;
    c.chokeEnabled     = true;
    c.crosstalkGroup   = 0;
    c.midiChannel      = 10;
    c.zone2MidiChannel = 10;
    c.ccNumber         = 4;
    c.ccChannel        = 10;

    // Tier-2 Edrumulus params (KD8-derived, real units in fixed-point — see Config.h)
    c.preScanTimeMs         = 25;    // 2.5 ms
    c.firstPeakDiffThreshDb = 80;    // 8.0 dB
    c.decayLen1Ms           = 0;     // 0 ms
    c.decayGradFact1        = 200;
    c.decayLen2Ms           = 3500;  // 350 ms
    c.decayGradFact2        = 450;   // KD8
    c.decayLen3Ms           = 5000;  // 500 ms (KD8)
    c.decayGradFact3        = 45;    // KD8
    c.decayFactDb           = 10;    // 1.0 dB
    c.maskTimeDecayFactDb   = 100;   // 10.0 dB (KD8)
    c.decayEstDelayMs       = 70;    // 7.0 ms  [unused until 2b]
    c.decayEstLenMs         = 40;    // 4.0 ms  [unused until 2b]
    c.decayEstFactDb        = 160;   // 16.0 dB [unused until 2b]
    c.clipCompAmpmapStep    = 8;     // 0.08    [unused until 2b]

    switch (idx) {
        case 0:  c.midiNote = 36; c.zone2MidiNote = 36; break;  // kick
        case 1:  c.midiNote = 38; c.zone2MidiNote = 40; break;  // snare head / snare rim
        case 2:  c.midiNote = 42; c.zone2MidiNote = 46; break;  // hi-hat closed / open
        case 3:  c.midiNote = 51; c.zone2MidiNote = 53; break;  // ride / ride bell
        case 4:  c.midiNote = 44; c.zone2MidiNote = 44; break;  // hi-hat foot pedal (CC)
        default: c.midiNote = 38; c.zone2MidiNote = 38; break;
    }
    return c;
}

void configResetDefaults() {
    for (uint8_t i = 0; i < NUM_INPUTS; i++) {
        g_inputs[i] = defaultInput(i);
    }
}

void configInit() {
    if (!LittleFS.begin()) {
        Serial.println("[Config] LittleFS mount failed - formatting...");
        LittleFS.format();
        if (!LittleFS.begin()) {
            Serial.println("[Config] LittleFS mount failed after format");
            return;
        }
        Serial.println("[Config] LittleFS formatted and mounted");
    } else {
        Serial.println("[Config] LittleFS mounted");
    }
}

void configLoad() {
    File f = LittleFS.open(CFG_FILE, "r");
    if (!f || f.size() != sizeof(g_inputs)) {
        Serial.println("[Config] No valid config file - using defaults");
        if (f) f.close();
        configResetDefaults();
        return;
    }

    f.read((uint8_t*)g_inputs, sizeof(g_inputs));
    f.close();
    Serial.println("[Config] Loaded from LittleFS");
}

void configSave() {
    File f = LittleFS.open(CFG_FILE, "w");
    if (!f) {
        Serial.println("[Config] Failed to open config file for writing");
        return;
    }

    f.write((uint8_t*)g_inputs, sizeof(g_inputs));
    f.close();
    Serial.println("[Config] Saved to LittleFS");
}

bool presetLoad(uint8_t id) {
    if (id >= MAX_PRESETS) return false;
    char path[16];
    presetPath(id, path);

    File f = LittleFS.open(path, "r");
    if (!f || f.size() != sizeof(Preset)) {
        if (f) f.close();
        return false;
    }

    Preset p;
    f.read((uint8_t*)&p, sizeof(p));
    f.close();
    memcpy(g_inputs, p.inputs, sizeof(g_inputs));
    return true;
}

bool presetRead(uint8_t id, Preset* out) {
    if (id >= MAX_PRESETS || !out) return false;
    char path[16];
    presetPath(id, path);

    File f = LittleFS.open(path, "r");
    if (!f || f.size() != sizeof(Preset)) {
        if (f) f.close();
        return false;
    }

    f.read((uint8_t*)out, sizeof(Preset));
    f.close();
    out->name[PRESET_NAME_LEN] = '\0';
    return true;
}

bool presetSave(uint8_t id, const char* name) {
    if (id >= MAX_PRESETS) return false;
    char path[16];
    presetPath(id, path);

    Preset p;
    strncpy(p.name, name, PRESET_NAME_LEN);
    p.name[PRESET_NAME_LEN] = '\0';
    memcpy(p.inputs, g_inputs, sizeof(g_inputs));

    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.write((uint8_t*)&p, sizeof(p));
    f.close();
    return true;
}

bool presetDelete(uint8_t id) {
    if (id >= MAX_PRESETS) return false;
    char path[16];
    presetPath(id, path);

    return LittleFS.remove(path);
}