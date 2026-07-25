#include "HiHat.h"

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

uint8_t HiHat::mapAndQuantize(float raw) {
    // Linear map [kAdcUp, kAdcDown] -> [0, 127], clamped at both ends. A harder
    // press than the measured 3400 just saturates at 127 rather than overshooting.
    float span = (float)(kAdcDown - kAdcUp);
    float cc   = (raw - (float)kAdcUp) * 127.0f / span;
    if (cc <= 0.0f)   return 0;
    if (cc >= 127.0f) cc = 127.0f;

    int mapped = (int)cc;

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
