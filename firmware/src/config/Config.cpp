#include "Config.h"
#include <string.h>
#include <stdio.h>

InputConfig g_inputs[NUM_INPUTS];

static const char* CFG_NS  = "edrum_cfg";
static const char* PRE_NS  = "edrum_pre";
static const char* CFG_KEY = "inputs";

static void presetKey(uint8_t id, char* buf) {
    snprintf(buf, 4, "p%X", id & 0x0F);
}

static InputConfig defaultInput(uint8_t idx) {
    InputConfig c = {};   // zero all fields first
    c.linkedInput      = 0xFF;    // 0xFF = no link
    c.padType          = 0;       // piezo single zone
    c.threshold        = 512;
    c.velocityCurve    = 0;       // linear
    c.retriggerTime    = 50;      // ms
    c.crosstalkGroup   = 0;
    c.midiNote         = 36 + idx;
    c.midiChannel      = 1;
    c.zone2MidiNote    = 37 + idx;
    c.zone2MidiChannel = 1;
    c.ccNumber         = 4;       // hihat pedal CC
    c.ccChannel        = 1;
    return c;
}

void configResetDefaults() {
    for (uint8_t i = 0; i < NUM_INPUTS; i++) {
        g_inputs[i] = defaultInput(i);
    }
}

void configLoad() {
    Preferences prefs;
    prefs.begin(CFG_NS, /*readOnly=*/true);
    size_t stored = prefs.getBytesLength(CFG_KEY);
    if (stored == sizeof(g_inputs)) {
        prefs.getBytes(CFG_KEY, g_inputs, sizeof(g_inputs));
    } else {
        // Nothing valid in NVS yet — start from defaults
        configResetDefaults();
    }
    prefs.end();
}

void configSave() {
    Preferences prefs;
    prefs.begin(CFG_NS, /*readOnly=*/false);
    prefs.putBytes(CFG_KEY, g_inputs, sizeof(g_inputs));
    prefs.end();
}

bool presetLoad(uint8_t id) {
    if (id >= MAX_PRESETS) return false;
    char key[4];
    presetKey(id, key);

    Preferences prefs;
    prefs.begin(PRE_NS, /*readOnly=*/true);
    size_t stored = prefs.getBytesLength(key);
    bool ok = false;
    if (stored == sizeof(Preset)) {
        Preset p;
        prefs.getBytes(key, &p, sizeof(p));
        memcpy(g_inputs, p.inputs, sizeof(g_inputs));
        ok = true;
    }
    prefs.end();
    return ok;
}

bool presetSave(uint8_t id, const char* name) {
    if (id >= MAX_PRESETS) return false;
    char key[4];
    presetKey(id, key);

    Preset p;
    strncpy(p.name, name, PRESET_NAME_LEN);
    p.name[PRESET_NAME_LEN] = '\0';
    memcpy(p.inputs, g_inputs, sizeof(g_inputs));

    Preferences prefs;
    prefs.begin(PRE_NS, /*readOnly=*/false);
    prefs.putBytes(key, &p, sizeof(p));
    prefs.end();
    return true;
}

bool presetRead(uint8_t id, Preset* out) {
    if (id >= MAX_PRESETS) return false;
    char key[4];
    presetKey(id, key);

    Preferences prefs;
    prefs.begin(PRE_NS, /*readOnly=*/true);
    size_t stored = prefs.getBytesLength(key);
    bool ok = false;
    if (stored == sizeof(Preset)) {
        prefs.getBytes(key, out, sizeof(Preset));
        out->name[PRESET_NAME_LEN] = '\0'; // guard against corrupt NVS
        ok = true;
    }
    prefs.end();
    return ok;
}

bool presetDelete(uint8_t id) {
    if (id >= MAX_PRESETS) return false;
    char key[4];
    presetKey(id, key);

    Preferences prefs;
    prefs.begin(PRE_NS, /*readOnly=*/false);
    bool ok = prefs.remove(key);
    prefs.end();
    return ok;
}
