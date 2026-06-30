#pragma once
#include <Arduino.h>

// ===========================================================================
// SpikeCancel — single-channel ADC spike canceller.
// ===========================================================================
// Ported from Edrumulus (Volker Fischer, GPL-2.0) `cancel_ADC_spikes()`:
//   D:\Dev\E-drums\eDrumulus\edrumulus-src\edrumulus\edrumulus_hardware.cpp
//
// The original keeps [pad_index][input_channel_index] history arrays; here each
// PDrum2Trigger owns exactly one head channel, so the indexing collapses to one
// set of prev-state / prev-sample members.
//
// Removes 1–4 sample spikes (the kind the bare ESP32 ADC front-end produces when
// it lacks an RC anti-alias/clamp filter, which our current boards do). Adds a
// processing latency of `level` samples (≈4 at level 4). Operates on a float
// sample + overload flag, both updated in place; the returned sample is delayed.
// ---------------------------------------------------------------------------

class SpikeCancel {
public:
    void reset();

    // Process one sample. `signal` and `overloadDetected` are updated in place to
    // the (delayed) de-spiked values. `level` (1..4) selects how many consecutive
    // spike samples can be removed; we use 4 on the ESP32.
    void process(float& signal, int& overloadDetected, int level);

private:
    enum SpikeState : uint8_t { ST_NOISE, ST_SPIKE_HIGH, ST_SPIKE_LOW, ST_OTHER };

    static constexpr int   kMaxNoiseAmpl   = 8;    // ADC_MAX_NOISE_AMPL
    static constexpr float kMaxPeakThresh  = 150;  // max assumed ESP32 spike amplitude

    SpikeState prev1State_ = ST_NOISE, prev2State_ = ST_NOISE, prev3State_ = ST_NOISE,
               prev4State_ = ST_NOISE, prev5State_ = ST_NOISE;
    float prevInput1_ = 0.0f, prevInput2_ = 0.0f, prevInput3_ = 0.0f, prevInput4_ = 0.0f;
    int   prevOverload1_ = 0, prevOverload2_ = 0, prevOverload3_ = 0, prevOverload4_ = 0;
};
