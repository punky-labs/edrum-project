#pragma once

#include <Arduino.h>

// Hi-hat pedal openness -> MIDI CC, v1.
//
// Deliberately NOT a TriggerEngine implementation. The pad engines detect
// transient events (threshold/scan/mask); the hi-hat pedal is a continuous
// POSITION sensor (FSR under the foot). Different processing model entirely,
// so no shared interface — a small self-contained class instead.
//
// Signal path: raw ADC (stream channel 0 = GPIO1) -> EMA smooth -> linear map
// to 0-127 -> 7-step quantize (HelloDrum FSRSensing table). A CC is emitted
// only when the quantized value CHANGES; that quantization is what prevents
// MIDI flooding, so no separate hysteresis/debounce timer is needed.
//
// Polarity: pressure INCREASES the ADC value, and CC 0 = fully open /
// CC 127 = fully closed (DW/eDRUMin convention), so the mapping is direct —
// no inversion anywhere.
//
// Calibration bounds are hardcoded constants for now (measured, not guessed);
// persisted/SysEx/telnet-tunable calibration is future work.
class HiHat {
public:
    void processBlock(const uint16_t* samples, uint16_t n);

    bool    hasCcChange() const { return ccChanged_; }
    void    clearCcChange()     { ccChanged_ = false; }
    uint8_t getCcValue() const  { return pendingCc_; }

    // Debug getters for telnet visibility (mirrors PDrumTrigger's getDebug*()).
    float getDebugSmoothed() const { return smoothed_; }
    int   getDebugRawLast() const  { return rawLast_; }

private:
    static constexpr int   kAdcUp    = 0;      // measured: pedal up / resting
    static constexpr int   kAdcDown  = 3400;   // measured: pedal fully pressed
    static constexpr float kEmaAlpha = 0.15f;  // heavier smoothing than the pad
                                               // EMA (default 0.5) is fine here:
                                               // slow position signal, not a fast
                                               // transient — no attack to blunt.

    float   smoothed_    = 0.0f;
    bool    initialized_ = false;   // first sample seeds smoothed_ directly, so
                                    // there's no artificial ramp-in at boot.
    int     rawLast_     = 0;       // last raw ADC sample seen (debug only)
    uint8_t lastCc_      = 0;       // last quantized value we compared against
    uint8_t pendingCc_   = 0;       // value to emit on the current change
    bool    ccChanged_   = false;

    static uint8_t mapAndQuantize(float raw);
};
