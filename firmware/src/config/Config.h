#pragma once
#include <Arduino.h>
#include <LittleFS.h>

#define NUM_INPUTS      5
#define MAX_PRESETS     16
#define PRESET_NAME_LEN 16

extern bool g_serialQuiet;

struct __attribute__((packed)) InputConfig {
    uint8_t  padType;
    bool     enabled;        // false = input ignored entirely (no detection/MIDI).
                             // Lets unpopulated jacks be silenced so a floating
                             // (unplugged, high-impedance) input can't generate
                             // phantom hits from antenna noise. Default true.
    uint16_t threshold;      // 0–1023 (ADC range); encode as 2x 7-bit bytes in SysEx
    uint8_t  velocityCurve;
    uint16_t retriggerTime;  // repurposed as retrigger-cancel MARGIN in raw ADC
                             // counts (v2); wire format unchanged (2x 7-bit bytes)
                             // and SysEx label stays 'retrigger time' for
                             // compatibility. Was v1's samples-to-peak cutoff.
    uint16_t headSensitivity;  // upper ADC bound for velocity scaling; default 1000
    uint16_t scanTime;         // v3: repurposed as confirmation-based Scan's HARD-CAP
                               // ms (force-commit backstop). Old fixed-window meaning
                               // retired — same repurposing pattern as retriggerTime.
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

    // ---- Tier-2 (Edrumulus) params — added Stage 2a for PDrum2Trigger. Stored in
    // REAL units (fixed-point: *Ms/*Db fields value×10 (one decimal); gradFact fields
    // integer ×1; clipCompAmpmapStep value×100). UNUSED by the active PDrumTrigger
    // engine (Phase-1 revert) — kept so a future return to PDrum2Trigger needs no
    // config-struct change. Tune via serial `w` / presets when that engine is active.
    // NOTE: `threshold` and `headSensitivity` (declared above) are RAW ADC units for
    // PDrumTrigger (a peak detector), NOT the Edrumulus 0..31 log-scale velocity_
    // threshold/velocity_sensitivity that PDrum2Trigger interpreted them as.
    uint16_t preScanTimeMs;         // ms×10   (25  = 2.5 ms)
    uint16_t firstPeakDiffThreshDb; // dB×10   (80  = 8.0 dB)
    uint16_t decayLen1Ms;           // ms×10   (0)
    uint16_t decayGradFact1;        // ×1      (200)
    uint16_t decayLen2Ms;           // ms×10   (3500 = 350 ms)
    uint16_t decayGradFact2;        // ×1      (450)
    uint16_t decayLen3Ms;           // ms×10   (5000 = 500 ms)
    uint16_t decayGradFact3;        // ×1      (45)
    uint16_t decayFactDb;           // dB×10   (10  = 1.0 dB)
    uint16_t maskTimeDecayFactDb;   // dB×10   (100 = 10.0 dB)
    uint16_t decayEstDelayMs;       // ms×10   (70)  [stored, UNUSED until 2b]
    uint16_t decayEstLenMs;         // ms×10   (40)  [stored, UNUSED until 2b]
    uint16_t decayEstFactDb;        // dB×10   (160) [stored, UNUSED until 2b]
    uint16_t clipCompAmpmapStep;    // ×100    (8   = 0.08) [stored, UNUSED until 2b]

    // ---- v3 Scan-redesign / EMA tunables (2026-07-12). Live-settable via telnet
    // `w` and LittleFS-persisted like every field above, but DELIBERATELY NOT wired
    // into the SysEx protocol (telnet-only bootstrap while the values are still being
    // found — same path `retrig` followed before any UI existed). Appended at the end
    // of the struct so existing field offsets are unchanged; adding them still grows
    // the struct, so a LittleFS re-upload (uploadfs) / config reset is required.
    uint16_t scanMargin;    // raw ADC counts — Scan confirmation prominence
    uint16_t settleWaitMs;  // ms — Scan settle-exit wait (no new confirmed peak)
    uint16_t emaAlpha;      // EMA alpha ×100 (0..100); 0 or 100 = smoothing off
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
