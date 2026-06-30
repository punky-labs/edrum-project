/*
  PDrum2Trigger — Stage 2a implementation.

  Detection core ported from Edrumulus (Volker Fischer, GPL-2.0):
    Edrumulus::Pad::initialize()      -> buildDerived()
    Edrumulus::Pad::process_sample()  -> processBlock() per-sample loop
  Single head-sensor path only. Clip/overload correction, adaptive decay power
  estimation, positional sensing, rim and choke are intentionally NOT ported here
  (Stages 2b/2c/2d) — see the TODO markers in processBlock().
*/

#include "PDrum2Trigger.h"
#include "Arduino.h"
#include <math.h>

// Band-pass filter coefficients (8 kHz design). Constant — from edrumulus.h.
const float PDrum2Trigger::bp_filt_a_[4] =
    { 0.6704579059531744f, -2.930427216820138f, 4.846289804288025f, -3.586239808116909f };
const float PDrum2Trigger::bp_filt_b_[5] =
    { 0.01658193166930305f, 0.0f, -0.0331638633386061f, 0.0f, 0.01658193166930305f };

PDrum2Trigger::PDrum2Trigger(byte headCh, byte rimCh)
    : headCh_(headCh), rimCh_(rimCh) {}

PDrum2Trigger::~PDrum2Trigger() {
    if (decay_) { free(decay_); decay_ = nullptr; }
}

void PDrum2Trigger::initialize(uint32_t sampleRateHz) {
    Fs_        = sampleRateHz ? (int)sampleRateHz : 8000;
    needsInit_ = true;
    buildDerived();
}

// Port of Edrumulus::Pad::initialize() — only the single-head-sensor derivations.
void PDrum2Trigger::buildDerived() {
    const int Fs = Fs_;

    const float threshold_db = 20.0f * log10f((float)kAdcMaxNoise) - 16.0f + (float)velThreshold_;
    threshold_               = powf(10.0f, threshold_db / 10.0f);              // linear power threshold
    first_peak_diff_thresh_  = powf(10.0f, (firstPeakDiffThreshDb_x10_ / 10.0f) / 10.0f);
    scan_time_               = (int)lroundf(scanTimeMs_ * 1e-3f * Fs);
    pre_scan_time_           = (int)lroundf((preScanTimeMs_x10_ / 10.0f) * 1e-3f * Fs);
    total_scan_time_         = scan_time_ + pre_scan_time_;
    mask_time_               = (int)lroundf(maskTimeMs_ * 1e-3f * Fs);
    decay_len1_              = (int)lroundf((decayLen1Ms_x10_ / 10.0f) * 1e-3f * Fs);
    decay_len2_              = (int)lroundf((decayLen2Ms_x10_ / 10.0f) * 1e-3f * Fs);
    decay_len3_              = (int)lroundf((decayLen3Ms_x10_ / 10.0f) * 1e-3f * Fs);
    decay_len_               = decay_len1_ + decay_len2_ + decay_len3_;
    decay_fact_              = powf(10.0f, (decayFactDb_x10_ / 10.0f) / 10.0f);
    decay_mask_fact_         = powf(10.0f, (maskTimeDecayFactDb_x10_ / 10.0f) / 10.0f);
    const float decay_grad1  = (float)decayGradFact1_ / Fs;
    const float decay_grad2  = (float)decayGradFact2_ / Fs;
    const float decay_grad3  = (float)decayGradFact3_ / Fs;
    x_sq_hist_len_           = max(1, total_scan_time_);

    // velocity curve (Edrumulus continuous formula; replaces the Stage-1 LUT)
    const float max_velocity_range_db = 20.0f * log10f((float)kAdcMaxRange / 2.0f) - threshold_db;
    const float velocity_range_db     = max_velocity_range_db * (32 - velSensitivity_) / 32.0f;
    float curve_param = 1.018f; // close to Roland "linear"
    switch (curveType_) {
        case 1: curve_param *= 1.012f; break; // EXP1
        case 2: curve_param *= 1.017f; break; // EXP2
        case 3: curve_param *= 0.995f; break; // LOG1
        case 4: curve_param *= 0.987f; break; // LOG2
        default: break;                       // LINEAR (0) / Custom (5) -> linear
    }
    velocity_factor_   = 126.0f / ((powf(curve_param, 126.0f) - 1) * curve_param *
                         powf(threshold_, 1270.0f / velocity_range_db * log10f(curve_param)));
    velocity_exponent_ = 1270.0f / velocity_range_db * log10f(curve_param);
    velocity_offset_   = 1.0f - 126.0f / (powf(curve_param, 126.0f) - 1);

    // Build the 3-segment decay curve LUT. It can be large (KD8 ≈ 6800 floats ≈
    // 27 KB; ×4 engines), so place it in PSRAM (this board has BOARD_HAS_PSRAM)
    // and keep it off the internal heap; fall back to internal RAM if PSRAM is
    // unavailable. The LUT is read once per sample in the hot loop — PSRAM latency
    // is fine at 8 kHz.
    if (decay_) { free(decay_); decay_ = nullptr; }
    const int alloc = max(1, decay_len_);
    decay_ = (float*)ps_malloc(sizeof(float) * alloc);
    if (!decay_) decay_ = (float*)malloc(sizeof(float) * alloc);
    if (!decay_) {
        // Out of memory: disable the decay model rather than dereference null.
        decay_len_ = decay_len1_ = decay_len2_ = decay_len3_ = 0;
    } else {
        for (int i = 0; i < alloc; i++) decay_[i] = 0.0f;
        for (int i = 0; i < decay_len1_; i++) {
            decay_[i] = powf(10.0f, -i / 10.0f * decay_grad1);
        }
        const float decay_fact1 = powf(10.0f, -decay_len1_ / 10.0f * decay_grad1);
        for (int i = 0; i < decay_len2_; i++) {
            decay_[decay_len1_ + i] = decay_fact1 * powf(10.0f, -i / 10.0f * decay_grad2);
        }
        const float decay_fact2 = decay_fact1 * powf(10.0f, -decay_len2_ / 10.0f * decay_grad2);
        for (int i = 0; i < decay_len3_; i++) {
            decay_[decay_len1_ + decay_len2_ + i] = decay_fact2 * powf(10.0f, -i / 10.0f * decay_grad3);
        }
    }

    x_sq_hist_.initialize(x_sq_hist_len_);
    resetState();
    needsInit_ = false;
}

void PDrum2Trigger::resetState() {
    for (int i = 0; i < kBpFiltLen; i++)     bp_filt_hist_x_[i] = 0.0f;
    for (int i = 0; i < kBpFiltLen - 1; i++) bp_filt_hist_y_[i] = 0.0f;
    mask_back_cnt_       = 0;
    decay_back_cnt_      = 0;
    scan_time_cnt_       = 0;
    max_x_filt_val_      = 0.0f;
    max_mask_x_filt_val_ = 0.0f;
    first_peak_val_      = 0.0f;
    peak_val_            = 0.0f;
    was_above_threshold_ = false;
    was_peak_found_      = false;
    decay_scaling_       = 1.0f;
    first_peak_delay_    = 0;
    dcSeeded_            = false;   // re-seed DC estimate from next sample
    spike_.reset();
}

// Port of Edrumulus::Pad::process_sample(), single head sensor path.
void PDrum2Trigger::processBlock(const uint16_t* headBlock, const uint16_t* /*rimBlock*/,
                                 uint16_t n, uint32_t blockStartAbsIndex) {
    hit_    = false;   // per-block results reset; chokeDetected_ latch cleared by main
    hitRim_ = false;

    if (needsInit_) buildDerived();
    if (!headBlock || n == 0) return;

    bool           firedThisBlock = false;
    const uint32_t blockEndAbs    = blockStartAbsIndex + n - 1;

    for (uint16_t j = 0; j < n; j++) {
        const uint32_t absIdx = blockStartAbsIndex + j;

        // 0. DC-offset removal (Edrumulus does this in process() BEFORE process_sample,
        //    plus a startup estimate that seeds dc_offset to the resting baseline).
        //    Our unipolar front-end rests at a positive DC bias; without removing it the
        //    band-pass transient response keeps crossing threshold -> phantom hits on
        //    every channel. One-pole IIR: track the baseline, subtract it. dcSeeded_
        //    seeds the estimate from the first sample to avoid a startup convergence
        //    transient (which would itself cause a burst of false fires).
        const float raw = (float)headBlock[j];
        if (!dcSeeded_) { dcOffset_ = raw; dcSeeded_ = true; }
        else            { dcOffset_ = kDcIirGamma * dcOffset_ + kDcIirOneMinusGamma * raw; }

        // 1. spike cancellation — now valid because `input` is zero-centred.
        float input    = raw - dcOffset_;
        int   overload = (raw >= (float)(kAdcMaxRange - kAdcMaxNoise)) ? 2
                       : (raw <= (float)(kAdcMaxNoise - 1))            ? 1 : 0;
        spike_.process(input, overload, kSpikeLevel);

        first_peak_delay_++; // increments each sample; reset at scan end

        // store raw power in FIFO (used for first-peak/max-peak picking)
        x_sq_hist_.add(input * input);

        // 2. band-pass IIR filter, then square -> power domain (x_filt)
        update_fifo(input, kBpFiltLen, bp_filt_hist_x_);
        float sum_b = 0.0f, sum_a = 0.0f;
        for (int i = 0; i < kBpFiltLen; i++)     sum_b += bp_filt_hist_x_[i] * bp_filt_b_[i];
        for (int i = 0; i < kBpFiltLen - 1; i++) sum_a += bp_filt_hist_y_[i] * bp_filt_a_[i];
        float x_filt = sum_b - sum_a;
        update_fifo(x_filt, kBpFiltLen - 1, bp_filt_hist_y_);
        x_filt = x_filt * x_filt;

        // 4. exponential decay subtraction (the retrigger mask — the runaway fix)
        float x_filt_decay = x_filt;
        if (decay_back_cnt_ > 0) {
            const float cur_decay = decay_scaling_ * decay_[decay_len_ - decay_back_cnt_];
            x_filt_decay = x_filt - cur_decay;
            decay_back_cnt_--;
            if (x_filt_decay < 0.0f) x_filt_decay = 0.0f;
        }

        // 5. mask-time rescue: a loud hit shortly after a soft hit still registers
        if ((mask_back_cnt_ > 0) && (mask_back_cnt_ <= mask_time_)) {
            if (x_filt > max_mask_x_filt_val_ * decay_mask_fact_) {
                was_above_threshold_ = false; // reset peak detection
                x_filt_decay         = x_filt; // remove decay subtraction
            }
        }

        // 6. threshold test + scan state machine
        if ((x_filt_decay > threshold_) || was_above_threshold_) {
            if (!was_above_threshold_) {
                // first sample above threshold for this peak
                scan_time_cnt_       = max(1, scan_time_ - kXFiltDelay);
                mask_back_cnt_       = scan_time_ + mask_time_;
                decay_back_cnt_      = 0;
                max_x_filt_val_      = x_filt;
                max_mask_x_filt_val_ = x_filt;
                was_above_threshold_ = true;
            }

            if (x_filt > max_x_filt_val_) max_x_filt_val_ = x_filt;
            if ((mask_back_cnt_ > mask_time_) && (x_filt > max_mask_x_filt_val_)) {
                max_mask_x_filt_val_ = x_filt;
            }

            scan_time_cnt_--;
            mask_back_cnt_--;

            // end of scan time: pick first peak (timing) and max peak (velocity)
            if (scan_time_cnt_ == 0) {
                bool  first_peak_found = false;
                first_peak_val_        = x_sq_hist_[x_sq_hist_len_ - total_scan_time_];
                int   first_peak_idx   = 0;
                for (int idx = 1; idx < total_scan_time_; idx++) {
                    const float cur  = x_sq_hist_[x_sq_hist_len_ - total_scan_time_ + idx];
                    const float prev = x_sq_hist_[x_sq_hist_len_ - total_scan_time_ + idx - 1];
                    if ((first_peak_val_ < cur) && !first_peak_found) {
                        first_peak_val_ = cur;
                        first_peak_idx  = idx;
                    } else {
                        first_peak_found = true;
                        // check if there is a much larger first peak
                        if ((prev > cur) && (first_peak_val_ * first_peak_diff_thresh_ < prev)) {
                            first_peak_val_ = prev;
                            first_peak_idx  = idx - 1;
                        }
                    }
                }

                // max velocity within scan time (unfiltered power)
                peak_val_ = 0.0f;
                for (int i = 0; i < scan_time_; i++) {
                    const float v = x_sq_hist_[x_sq_hist_len_ - scan_time_ + i];
                    if (v > peak_val_) peak_val_ = v;
                }

                first_peak_delay_ = total_scan_time_ - (first_peak_idx + 1);
                was_peak_found_   = true;

                // TODO 2b: clip/overload correction (overload-history walk +
                //          amplification_mapping) slots in here.
            }

            // end of mask time: arm the decay model for retrigger suppression
            if (mask_back_cnt_ == 0) {
                decay_back_cnt_      = decay_len_;
                decay_scaling_       = decay_fact_ * max_x_filt_val_;
                was_above_threshold_ = false;
            }
        }

        // TODO 2b: adaptive decay power estimation (decay_pow_est_*) slots in here.
        // TODO 2c/2d: positional sensing / dual-piezo rim / switch-choke slot in here.

        // peak found -> emit (single head sensor: no pos/rim delay, fire immediately)
        if (was_peak_found_) {
            int midi_vel = (int)(velocity_factor_ *
                           powf(peak_val_ * kNoiseVelScale, velocity_exponent_) + velocity_offset_);
            midi_vel       = max(1, min(127, midi_vel));
            velocity_      = midi_vel;
            velocityRaw_   = (int)sqrtf(peak_val_); // amplitude units for main's rawToMidi/debug
            hit_           = true;
            firedThisBlock = true;
            crossAbsIndex_ = (absIdx >= (uint32_t)first_peak_delay_) ? (absIdx - first_peak_delay_) : 0;
            was_peak_found_ = false;
        }
    }

    if (firedThisBlock) {
        triggerBack_ = (blockEndAbs >= crossAbsIndex_) ? (blockEndAbs - crossAbsIndex_) : 0;
    }
}
