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
    c.padType          = 0;
    c.threshold        = 512;
    c.velocityCurve    = 0;
    c.retriggerTime    = 50;
    c.headSensitivity  = 1000;
    c.scanTime         = 10;
    c.maskTime         = 30;
    c.rimSensitivity   = 200;
    c.rimThreshold     = 30;
    c.crosstalkGroup   = 0;
    c.midiNote         = 36 + idx;
    c.midiChannel      = 1;
    c.zone2MidiNote    = 37 + idx;
    c.zone2MidiChannel = 1;
    c.ccNumber         = 4;
    c.ccChannel        = 1;
    return c;
}

void configResetDefaults() {
    for (uint8_t i = 0; i < NUM_INPUTS; i++) {
        g_inputs[i] = defaultInput(i);
    }
}

void configLoad() {
    if (!LittleFS.begin()) {
        Serial.println("[Config] LittleFS mount failed - using defaults");
        configResetDefaults();
        return;
    }

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
    if (!LittleFS.begin()) {
        Serial.println("[Config] LittleFS mount failed - cannot save");
        return;
    }

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

    if (!LittleFS.begin()) return false;
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

    if (!LittleFS.begin()) return false;
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

    if (!LittleFS.begin()) return false;

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

    if (!LittleFS.begin()) return false;
    return LittleFS.remove(path);
}