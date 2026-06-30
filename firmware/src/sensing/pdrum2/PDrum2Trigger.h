/*
  PDrum2Trigger — Stage 2a: Edrumulus power-domain detection (SINGLE_PIEZO).

  Layer 3 of the DMA -> SampleStream -> engine -> MIDI pipeline. Pure DSP: it
  receives gapless per-channel sample blocks and emits hit/velocity. It knows
  nothing about ADC/DMA/GPIO.

  The detection core is ported from Edrumulus (Volker Fischer, GPL-2.0),
  `Edrumulus::Pad::initialize()` + `Edrumulus::Pad::process_sample()`, single
  head-sensor path only:
    spike-cancel -> band-pass IIR -> square (power) -> 3-segment decay-model
    retrigger mask + mask-time rescue -> threshold/scan state machine ->
    first-peak (timing) + max-peak (velocity) -> velocity curve.

  Stage 2a SKIPS (TODO 2b/2c/2d, marked in the .cpp): clip/overload correction,
  adaptive decay power estimation, positional sensing, rim/choke, multi-sensor.
*/

#ifndef PDrum2Trigger_h
#define PDrum2Trigger_h

#include "Arduino.h"
#include "../TriggerEngine.h"
#include "../FastWriteFIFO.h"
#include "SpikeCancel.h"

class PDrum2Trigger : public TriggerEngine {
public:
    PDrum2Trigger(byte headCh, byte rimCh);
    ~PDrum2Trigger() override;

    // ----- TriggerEngine interface -----
    void initialize(uint32_t sampleRateHz) override;
    void processBlock(const uint16_t* headBlock, const uint16_t* rimBlock,
                      uint16_t n, uint32_t blockStartAbsIndex) override;

    bool hasHit()            const override { return hit_; }
    bool hasHitRim()         const override { return hitRim_; }
    bool hasChoke()          const override { return chokeDetected_; }
    void clearChoke()              override { chokeDetected_ = false; }

    int  getVelocity()       const override { return velocity_; }
    int  getVelocityRim()    const override { return velocityRim_; }
    int  getVelocityRaw()    const override { return velocityRaw_; }
    int  getVelocityRimRaw() const override { return velocityRimRaw_; }

    // Distance (samples) from the end of the most recent block back to the first
    // peak of the hit reported this block, in absolute sample space. main maps it:
    //   crossingAbsIndex = blockEndAbsIndex - getTriggerSnap().
    uint32_t getTriggerSnap() const override { return triggerBack_; }

    // Tier-1 setters (threshold/headSensitivity now carry Edrumulus 0..31 units).
    void setPadType(uint8_t t)             override { padType_           = t; }
    void setHeadThreshold(uint16_t v)      override { velThreshold_   = v; needsInit_ = true; }
    void setHeadSensitivity(uint16_t v)    override { velSensitivity_ = v; needsInit_ = true; }
    void setScanTime(uint16_t v)           override { scanTimeMs_  = v; needsInit_ = true; }
    void setMaskTime(uint16_t v)           override { maskTimeMs_  = v; needsInit_ = true; }
    void setCurveType(uint8_t v)           override { curveType_   = v; needsInit_ = true; }
    void setNoteHead(uint8_t v)            override { noteHead_          = v; }
    void setRimRatioThreshold(uint16_t v)  override { rimRatioThreshold_ = v; }
    void setChokeThreshold(uint16_t v)     override { chokeThreshold_    = v; }
    void setChokeEnabled(bool v)           override { chokeEnabled_      = v; }
    uint8_t getNoteHead()    const         override { return noteHead_; }

    // Tier-2 setters (fixed-point reals from InputConfig — see Config.h).
    void setPreScanTimeMs(uint16_t v)         override { preScanTimeMs_x10_      = v; needsInit_ = true; }
    void setFirstPeakDiffThreshDb(uint16_t v) override { firstPeakDiffThreshDb_x10_ = v; needsInit_ = true; }
    void setDecayLen1Ms(uint16_t v)           override { decayLen1Ms_x10_ = v; needsInit_ = true; }
    void setDecayGradFact1(uint16_t v)        override { decayGradFact1_  = v; needsInit_ = true; }
    void setDecayLen2Ms(uint16_t v)           override { decayLen2Ms_x10_ = v; needsInit_ = true; }
    void setDecayGradFact2(uint16_t v)        override { decayGradFact2_  = v; needsInit_ = true; }
    void setDecayLen3Ms(uint16_t v)           override { decayLen3Ms_x10_ = v; needsInit_ = true; }
    void setDecayGradFact3(uint16_t v)        override { decayGradFact3_  = v; needsInit_ = true; }
    void setDecayFactDb(uint16_t v)           override { decayFactDb_x10_ = v; needsInit_ = true; }
    void setMaskTimeDecayFactDb(uint16_t v)   override { maskTimeDecayFactDb_x10_ = v; needsInit_ = true; }
    void setDecayEstDelayMs(uint16_t v)       override { decayEstDelayMs_x10_ = v; /* 2b */ }
    void setDecayEstLenMs(uint16_t v)         override { decayEstLenMs_x10_   = v; /* 2b */ }
    void setDecayEstFactDb(uint16_t v)        override { decayEstFactDb_x10_  = v; /* 2b */ }
    void setClipCompAmpmapStep(uint16_t v)    override { clipCompAmpmapStep_x100_ = v; /* 2b */ }

private:
    void buildDerived();   // (re)compute all Fs-/config-dependent values + reset state
    void resetState();     // clear the per-sample detection state machine

    // ---- Ported Edrumulus constants ----
    static constexpr int   kBpFiltLen     = 5;
    static constexpr int   kXFiltDelay    = 5;
    static constexpr int   kAdcMaxRange   = 4096;
    static constexpr int   kAdcMaxNoise   = 8;     // ADC_MAX_NOISE_AMPL
    static constexpr float kNoiseVelScale = 1.0f / 6.0f; // ADC_noise_peak_velocity_scaling
    static constexpr int   kSpikeLevel    = 4;
    // DC-offset removal IIR (Edrumulus dc_offset_iir, tau = 0.5 s @ 8 kHz).
    // gamma = exp(-1/(Fs*tau)); precomputed for Fs=8000, tau=0.5 -> exp(-1/4000).
    // (Fixed for the locked 8 kHz design rate; recompute if Fs ever changes.)
    static constexpr float kDcIirGamma         = 0.99975003124f; // exp(-1/4000)
    static constexpr float kDcIirOneMinusGamma = 1.0f - kDcIirGamma;
    // band-pass filter coefficients (8 kHz design — constant, must not change)
    static const float bp_filt_a_[4];
    static const float bp_filt_b_[5];

    // Channel identity (informational for Stage 1/2a).
    byte headCh_;
    byte rimCh_;

    // ---- Config (Tier-1: 0..31 / ms; Tier-2: fixed-point as stored) ----
    uint8_t  padType_           = 2;     // SINGLE_PIEZO
    int      velThreshold_      = 8;     // velocity_threshold 0..31
    int      velSensitivity_    = 2;     // velocity_sensitivity 0..31
    uint16_t scanTimeMs_        = 3;     // ms
    uint16_t maskTimeMs_        = 6;     // ms
    uint8_t  curveType_         = 4;     // LOG2
    byte     noteHead_          = 36;
    uint16_t rimRatioThreshold_ = 40;
    uint16_t chokeThreshold_    = 50;
    bool     chokeEnabled_      = false;

    uint16_t preScanTimeMs_x10_      = 25;
    uint16_t firstPeakDiffThreshDb_x10_ = 80;
    uint16_t decayLen1Ms_x10_        = 0;
    uint16_t decayGradFact1_         = 200;
    uint16_t decayLen2Ms_x10_        = 3500;
    uint16_t decayGradFact2_         = 450;
    uint16_t decayLen3Ms_x10_        = 5000;
    uint16_t decayGradFact3_         = 45;
    uint16_t decayFactDb_x10_        = 10;
    uint16_t maskTimeDecayFactDb_x10_ = 100;
    uint16_t decayEstDelayMs_x10_    = 70;   // 2b
    uint16_t decayEstLenMs_x10_      = 40;   // 2b
    uint16_t decayEstFactDb_x10_     = 160;  // 2b
    uint16_t clipCompAmpmapStep_x100_ = 8;   // 2b

    // ---- Derived at buildDerived() ----
    int    Fs_              = 8000;
    bool   needsInit_       = true;
    float  threshold_       = 0.0f;   // linear power threshold
    int    scan_time_       = 0;
    int    pre_scan_time_   = 0;
    int    total_scan_time_ = 0;
    int    mask_time_       = 0;
    int    decay_len_       = 0;
    int    decay_len1_      = 0;
    int    decay_len2_      = 0;
    int    decay_len3_      = 0;
    float  decay_fact_      = 0.0f;
    float  decay_mask_fact_ = 0.0f;
    float  first_peak_diff_thresh_ = 0.0f;
    float  velocity_factor_   = 0.0f;
    float  velocity_exponent_ = 0.0f;
    float  velocity_offset_   = 0.0f;
    float* decay_           = nullptr;   // decay LUT (heap, length decay_len_)
    int    x_sq_hist_len_   = 1;

    // ---- Per-sample detection state ----
    SpikeCancel   spike_;
    FastWriteFIFO x_sq_hist_;
    float  bp_filt_hist_x_[kBpFiltLen]     = {0};
    float  bp_filt_hist_y_[kBpFiltLen - 1] = {0};
    int    mask_back_cnt_   = 0;
    int    decay_back_cnt_  = 0;
    int    scan_time_cnt_   = 0;
    float  max_x_filt_val_      = 0.0f;
    float  max_mask_x_filt_val_ = 0.0f;
    float  first_peak_val_  = 0.0f;
    float  peak_val_        = 0.0f;
    bool   was_above_threshold_ = false;
    bool   was_peak_found_  = false;
    float  decay_scaling_   = 1.0f;
    int    first_peak_delay_ = 0;

    // ---- DC-offset removal (re-centre the unipolar signal around zero) ----
    float  dcOffset_ = 0.0f;   // tracked baseline
    bool   dcSeeded_ = false;  // seed from first sample to avoid startup transient

    // ---- Block coordinate / results ----
    uint32_t crossAbsIndex_ = 0;   // absolute index of fired hit's first peak
    uint32_t triggerBack_   = 0;   // blockEndAbs - crossAbsIndex_ (set at block end)

    bool hit_            = false;
    bool hitRim_         = false;
    bool chokeDetected_  = false;
    int  velocity_       = 0;
    int  velocityRim_    = 0;
    int  velocityRaw_    = 0;
    int  velocityRimRaw_ = 0;
};

#endif
