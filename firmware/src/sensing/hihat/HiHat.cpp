#include "HiHat.h"
#include "../curve.h"   // applyCurve() — shared with PDrumTrigger's velocity curves

void HiHat::processBlock(const uint16_t* samples, uint16_t n) {
    if (n == 0) return;

    for (uint16_t i = 0; i < n; i++) {
        float s = (float)samples[i];
        if (!initialized_) {
            // Seed the filter with the first-ever sample rather than easing up
            // from 0, so boot doesn't emit a spurious "opening" ramp.
            smoothed_    = s;
            initialized_ = true;
        } else {
            smoothed_ += kEmaAlpha * (s - smoothed_);
        }
    }
    rawLast_ = (int)samples[n - 1];

    uint8_t cc = mapAndQuantize(smoothed_);
    if (cc != lastCc_) {
        lastCc_    = cc;
        pendingCc_ = cc;
        ccChanged_ = true;
    }
}

uint8_t HiHat::mapAndQuantize(float raw) const {
    // Map [kAdcUp, maxAdc_] -> [1,127] through the shared curve (same one the pad
    // velocities use), so the app's curve enum reshapes openness identically. A
    // harder press than maxAdc_ saturates at 127 rather than overshooting.
    //
    // NOTE: applyCurve()'s floor clamps to a minimum of 1 (every pad curve does
    // this — velocity 0 makes no sense for a hit). Pedal-fully-open therefore maps
    // to 1 before quantization, not 0. Harmless: the first quantize bucket is
    // <20 -> 0, so 1 still quantizes to CC 0 exactly as before. Not a bug — do not
    // "fix" it (see task doc 1b).
    uint8_t mapped = applyCurve((int)raw, kAdcUp, maxAdc_, curveType_);

    // 7-step quantize, exactly HelloDrum's FSRSensing() boundaries. This coarse
    // quantization (not a debounce timer) is what keeps the CC stream from
    // flooding: a new CC is sent only when the step changes.
    if      (mapped <  20) return 0;
    else if (mapped <  40) return 20;
    else if (mapped <  60) return 40;
    else if (mapped <  80) return 60;
    else if (mapped < 100) return 80;
    else if (mapped < 120) return 100;
    else                   return 127;
}
