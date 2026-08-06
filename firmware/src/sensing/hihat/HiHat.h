#pragma once

#include <Arduino.h>

// Hi-hat pedal openness -> MIDI CC, v1.
//
// Deliberately NOT a TriggerEngine implementation. The pad engines detect
// transient events (threshold/scan/mask); the hi-hat pedal is a continuous
// POSITION sensor (FSR under the foot). Different processing model entirely,
// so no shared interface — a small self-contained class instead.
//
// Signal path: raw ADC (stream channel 0 = GPIO1) -> EMA smooth -> applyCurve()
// (shared with the pad velocity curves) map to 0-127 -> 7-step quantize
// (HelloDrum FSRSensing table). A CC is emitted only when the quantized value
// CHANGES; that quantization is what prevents MIDI flooding, so no separate
// hysteresis/debounce timer is needed.
//
// Polarity: pressure INCREASES the ADC value, and CC 0 = fully open /
// CC 127 = fully closed (DW/eDRUMin convention), so the mapping is direct —
// no inversion anywhere.
//
// The max bound (raw ADC ceiling) and curve shape are runtime-configurable from
// the app, reusing g_inputs[4]'s existing headSensitivity/velocityCurve fields
// (applied via main's applyConfig()). Min (pedal-up) stays hardcoded at 0.
class HiHat {
public:
    void processBlock(const uint16_t* samples, uint16_t n);

    // Config, applied from main's applyConfig() (boot + every SysEx write).
    void setMaxAdc(int maxAdc)         { maxAdc_    = maxAdc; }
    void setCurveType(uint8_t curveType) { curveType_ = curveType; }

    bool    hasCcChange() const { return ccChanged_; }
    void    clearCcChange()     { ccChanged_ = false; }
    uint8_t getCcValue() const  { return pendingCc_; }

    // Debug getters for telnet visibility (mirrors PDrumTrigger's getDebug*()).
    float getDebugSmoothed() const { return smoothed_; }
    int   getDebugRawLast() const  { return rawLast_; }

private:
    static constexpr int   kAdcUp    = 0;      // pedal up / resting — stays
                                               // hardcoded (confirmed with Andrew).
    static constexpr float kEmaAlpha = 0.15f;  // heavier smoothing than the pad
                                               // EMA (default 0.5) is fine here:
                                               // slow position signal, not a fast
                                               // transient — no attack to blunt.

    int     maxAdc_      = 3400;    // runtime-settable; 3400 = real measured default,
                                    // matches Config.cpp's per-input default for jack 4.
    uint8_t curveType_   = 0;       // 0 = Natural (linear) — matches v1 behaviour.
    float   smoothed_    = 0.0f;
    bool    initialized_ = false;   // first sample seeds smoothed_ directly, so
                                    // there's no artificial ramp-in at boot.
    int     rawLast_     = 0;       // last raw ADC sample seen (debug only)
    uint8_t lastCc_      = 0;       // last quantized value we compared against
    uint8_t pendingCc_   = 0;       // value to emit on the current change
    bool    ccChanged_   = false;

    uint8_t mapAndQuantize(float raw) const;
};
