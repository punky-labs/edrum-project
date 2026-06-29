/*
  PDrum2Trigger — Stage 1 of the sensing rewrite.

  Layer 3 of the DMA -> SampleStream -> engine -> MIDI pipeline. Pure DSP: it
  receives gapless per-channel sample blocks and emits hit/velocity. It knows
  nothing about ADC/DMA/GPIO.

  Stage 1 scope: SINGLE_PIEZO only, using a simple time-domain peak detector
  (threshold crossing -> scan for peak -> velocity curve -> retrigger mask),
  all sample-count based (NOT millis()). The Edrumulus band-pass / power-domain
  / decay model is Stage 2 and is deliberately NOT implemented here.
*/

#ifndef PDrum2Trigger_h
#define PDrum2Trigger_h

#include "Arduino.h"
#include "../TriggerEngine.h"

class PDrum2Trigger : public TriggerEngine {
public:
    PDrum2Trigger(byte headCh, byte rimCh);

    // ----- TriggerEngine interface -----
    void initialize(uint32_t sampleRateHz) override;
    void processBlock(const uint16_t* headBlock, const uint16_t* rimBlock, uint16_t n) override;

    bool hasHit()            const override { return hit_; }
    bool hasHitRim()         const override { return hitRim_; }
    bool hasChoke()          const override { return chokeDetected_; }
    void clearChoke()              override { chokeDetected_ = false; }

    int  getVelocity()       const override { return velocity_; }
    int  getVelocityRim()    const override { return velocityRim_; }
    int  getVelocityRaw()    const override { return velocityRaw_; }
    int  getVelocityRimRaw() const override { return velocityRimRaw_; }

    // Distance (in samples) from the END of the most recently processed block back
    // to the threshold crossing of the hit reported this block. The caller composes
    // the SampleStream-absolute index of the crossing as:
    //   (absoluteIndexOfLastSampleInBlock - getTriggerSnap()).
    // A crossing and the scan that confirms it may straddle block boundaries, so
    // this is measured against the block end (which the caller knows absolutely),
    // not the block start.
    uint32_t getTriggerSnap() const override { return triggerBack_; }

    void setPadType(uint8_t t)             override { padType_           = t; }
    void setHeadThreshold(uint16_t v)      override { headThreshold_     = v; }
    void setHeadSensitivity(uint16_t v)    override { headSensitivity_   = v; }
    void setScanTime(uint16_t v)           override { scanTimeMs_  = v; recomputeTiming(); }
    void setMaskTime(uint16_t v)           override { maskTimeMs_  = v; recomputeTiming(); }
    void setCurveType(uint8_t v)           override { curveType_         = v; }
    void setNoteHead(uint8_t v)            override { noteHead_          = v; }
    void setRimRatioThreshold(uint16_t v)  override { rimRatioThreshold_ = v; }
    void setChokeThreshold(uint16_t v)     override { chokeThreshold_    = v; }
    void setChokeEnabled(bool v)           override { chokeEnabled_      = v; }
    uint8_t getNoteHead()    const         override { return noteHead_; }

private:
    enum State : uint8_t { IDLE, SCANNING, MASKED };

    void recomputeTiming();
    int  curve(int velocityRaw, int threshold, int sensRaw, byte curveType) const;

    // Channel identity (head/rim channel indices — informational for Stage 1).
    byte headCh_;
    byte rimCh_;

    // Config (set via setters from applyConfig()).
    uint8_t  padType_           = 2;     // SINGLE_PIEZO
    uint16_t headThreshold_     = 20;
    uint16_t headSensitivity_   = 800;
    uint16_t scanTimeMs_        = 3;
    uint16_t maskTimeMs_        = 50;
    byte     curveType_         = 0;
    byte     noteHead_          = 36;
    uint16_t rimRatioThreshold_ = 40;
    uint16_t chokeThreshold_    = 50;
    bool     chokeEnabled_      = false;

    // Derived (initialize()/recomputeTiming()).
    uint32_t sampleRateHz_ = 8000;
    uint32_t scanSamples_  = 24;
    uint32_t maskSamples_  = 400;

    // Detector state.
    State    state_        = IDLE;
    int      peak_         = 0;
    uint32_t scanRemain_   = 0;
    uint32_t maskRemain_   = 0;
    uint32_t procIndex_    = 0;    // monotonic count of samples processed
    uint32_t crossIndex_   = 0;    // procIndex_ at the in-progress scan's crossing
    uint32_t crossFired_   = 0;    // procIndex_ at the crossing of the hit fired this block
    uint32_t triggerBack_  = 0;    // samples from block end back to that crossing

    // Results (read by main after processBlock()).
    bool hit_            = false;
    bool hitRim_         = false;
    bool chokeDetected_  = false;
    int  velocity_       = 0;
    int  velocityRim_    = 0;
    int  velocityRaw_    = 0;
    int  velocityRimRaw_ = 0;
};

#endif
