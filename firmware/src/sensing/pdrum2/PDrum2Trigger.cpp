/*
  PDrum2Trigger — Stage 1 implementation (SINGLE_PIEZO, time-domain peak detector).

  Detection is a simple sample-count state machine (no millis()):
    IDLE     : watch head samples for value > headThreshold -> record crossing,
               seed peak, enter SCANNING for scanSamples.
    SCANNING : track the max sample; when scanSamples elapse, emit hit + velocity
               (via the curve LUT) and enter MASKED for maskSamples.
    MASKED   : ignore everything for maskSamples, then return to IDLE.

  rimBlock is ignored in Stage 1 (SINGLE_PIEZO has no rim). The band-pass /
  power-domain / decay model is Stage 2.
*/

#include "PDrum2Trigger.h"
#include "Arduino.h"

// Precomputed velocity-curve lookup tables [0..126] -> [1..127].
// Ported verbatim from PDrumTrigger (RyoKosaka HelloDrum lineage) — Stage 1
// reuses the existing curve approach; only the detector upstream changed.
static const uint8_t kCurveExp1[127] = {   // curve 1 — Expressive (base 1.02)
      1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  5,  5,
      5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10, 10, 11,
     11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18,
     19, 20, 20, 21, 21, 22, 23, 23, 24, 25, 25, 26, 27, 28, 28, 29,
     30, 31, 32, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
     45, 46, 47, 48, 49, 51, 52, 53, 54, 56, 57, 58, 60, 61, 63, 64,
     65, 67, 69, 70, 72, 73, 75, 77, 79, 80, 82, 84, 86, 88, 90, 92,
     94, 96, 98,100,102,105,107,109,112,114,117,119,122,124,127,
};
static const uint8_t kCurveExp2[127] = {   // curve 2 — Sensitive (base 1.05)
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6,  7,
      7,  7,  7,  8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13,
     14, 15, 15, 16, 17, 18, 19, 20, 21, 21, 23, 24, 25, 26, 27, 29,
     30, 31, 33, 35, 36, 38, 40, 42, 44, 46, 48, 51, 53, 56, 59, 61,
     65, 68, 71, 75, 78, 82, 86, 90, 95,100,105,110,115,121,127,
};
static const uint8_t kCurveLog1[127] = {   // curve 3 — Punchy (base 0.98)
      1,  4,  6,  9, 12, 14, 17, 19, 21, 24, 26, 28, 30, 33, 35, 37,
     39, 41, 43, 45, 46, 48, 50, 52, 54, 55, 57, 58, 60, 62, 63, 65,
     66, 68, 69, 70, 72, 73, 74, 76, 77, 78, 79, 80, 82, 83, 84, 85,
     86, 87, 88, 89, 90, 91, 92, 93, 94, 94, 95, 96, 97, 98, 99, 99,
    100,101,102,102,103,104,104,105,106,106,107,108,108,109,109,110,
    111,111,112,112,113,113,114,114,115,115,116,116,116,117,117,118,
    118,118,119,119,120,120,120,121,121,121,122,122,122,123,123,123,
    123,124,124,124,125,125,125,125,126,126,126,126,127,127,127,
};
static const uint8_t kCurveLog2[127] = {   // curve 4 — Aggressive (base 0.95)
      1,  7, 13, 19, 24, 30, 34, 39, 43, 48, 52, 55, 59, 62, 66, 69,
     72, 74, 77, 80, 82, 84, 86, 88, 90, 92, 94, 96, 97, 99,100,101,
    103,104,105,106,107,108,109,110,111,112,113,113,114,115,115,116,
    116,117,117,118,118,119,119,120,120,120,121,121,121,122,122,122,
    122,123,123,123,123,124,124,124,124,124,124,125,125,125,125,125,
    125,125,125,125,125,126,126,126,126,126,126,126,126,126,126,126,
    126,126,126,126,126,126,127,127,127,127,127,127,127,127,127,127,
    127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
};

PDrum2Trigger::PDrum2Trigger(byte headCh, byte rimCh)
    : headCh_(headCh), rimCh_(rimCh) {}

void PDrum2Trigger::initialize(uint32_t sampleRateHz) {
    sampleRateHz_ = sampleRateHz ? sampleRateHz : 8000;
    state_      = IDLE;
    peak_       = 0;
    scanRemain_ = 0;
    maskRemain_ = 0;
    procIndex_  = 0;
    recomputeTiming();
}

void PDrum2Trigger::recomputeTiming() {
    // ms -> samples (rounded), minimum 1 sample so a scan always makes progress.
    scanSamples_ = (uint32_t)scanTimeMs_ * sampleRateHz_ / 1000UL;
    maskSamples_ = (uint32_t)maskTimeMs_ * sampleRateHz_ / 1000UL;
    if (scanSamples_ < 1) scanSamples_ = 1;
}

void PDrum2Trigger::processBlock(const uint16_t* headBlock, const uint16_t* /*rimBlock*/, uint16_t n) {
    // Per-block results reset each call; chokeDetected_ is a latch cleared by main.
    hit_    = false;
    hitRim_ = false;

    if (!headBlock || n == 0) return;

    bool firedThisBlock = false;

    for (uint16_t j = 0; j < n; j++) {
        int v = (int)headBlock[j];

        switch (state_) {
            case IDLE:
                if (v > (int)headThreshold_) {
                    crossIndex_ = procIndex_;
                    peak_       = v;
                    scanRemain_ = scanSamples_;
                    state_      = SCANNING;
                }
                break;

            case SCANNING:
                if (v > peak_) peak_ = v;
                if (--scanRemain_ == 0) {
                    velocityRaw_ = peak_;
                    velocity_    = curve(peak_, (int)headThreshold_,
                                         (int)headSensitivity_, curveType_);
                    hit_           = true;
                    firedThisBlock = true;
                    crossFired_    = crossIndex_;
                    maskRemain_    = maskSamples_;
                    state_         = (maskSamples_ > 0) ? MASKED : IDLE;
                }
                break;

            case MASKED:
                if (--maskRemain_ == 0) state_ = IDLE;
                break;
        }

        procIndex_++;
    }

    // procIndex_ now indexes one past the block's last sample. Express the fired
    // hit's crossing as a distance back from that last sample so main can map it
    // to the SampleStream-absolute index it already knows for the block end.
    if (firedThisBlock) {
        triggerBack_ = (procIndex_ - 1) - crossFired_;
    }
}

int PDrum2Trigger::curve(int velocityRaw, int threshold, int sensRaw, byte curveType) const {
    float resF = map(velocityRaw, threshold, sensRaw, 1, 127);
    if (resF <= 1)   resF = 1;
    if (resF > 127)  resF = 127;

    if (curveType == 1)      { int idx = constrain((int)resF - 1, 0, 126); resF = kCurveExp1[idx]; }
    else if (curveType == 2) { int idx = constrain((int)resF - 1, 0, 126); resF = kCurveExp2[idx]; }
    else if (curveType == 3) { int idx = constrain((int)resF - 1, 0, 126); resF = kCurveLog1[idx]; }
    else if (curveType == 4) { int idx = constrain((int)resF - 1, 0, 126); resF = kCurveLog2[idx]; }
    // curve 0 (Natural/linear) and 5 (Custom -> linear) fall through unchanged.

    return (int)round(resF);
}
