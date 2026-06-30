#pragma once
#include <Arduino.h>

class TriggerEngine {
public:
    // One-time setup; Fs-dependent work (scan/mask sample counts, filters) here.
    virtual void initialize(uint32_t sampleRateHz) = 0;

    // Process a block of this engine's channel samples (gapless, from SampleStream).
    // headBlock/rimBlock each carry `n` samples for the head and rim channels.
    // blockStartAbsIndex is the SampleStream-absolute frame index of headBlock[0],
    // so the engine indexes detection in absolute sample space (no origin mismatch
    // when the caller maps getTriggerSnap() back to the ring).
    virtual void processBlock(const uint16_t* headBlock, const uint16_t* rimBlock,
                              uint16_t n, uint32_t blockStartAbsIndex) = 0;

    // Hit detection results — valid after sensing() returns
    virtual bool hasHit()          const = 0;
    virtual bool hasHitRim()       const = 0;
    virtual bool hasChoke()        const = 0;
    virtual void clearChoke()            = 0;

    // Velocity results
    virtual int  getVelocity()     const = 0;
    virtual int  getVelocityRim()  const = 0;
    virtual int  getVelocityRaw()  const = 0;
    virtual int  getVelocityRimRaw() const = 0;

    // Scope support: distance (in samples) from the end of the most recently
    // processed block back to the last reported hit's threshold crossing. The
    // caller composes the SampleStream-absolute crossing index from the block-end
    // index it already knows. Valid immediately after a processBlock() that set a hit.
    virtual uint32_t getTriggerSnap() const = 0;

    // Configuration — applied from g_inputs[] by applyConfig()
    virtual void setPadType(uint8_t t)               = 0;
    virtual void setHeadThreshold(uint16_t v)        = 0;
    virtual void setHeadSensitivity(uint16_t v)      = 0;
    virtual void setScanTime(uint16_t v)             = 0;
    virtual void setMaskTime(uint16_t v)             = 0;
    virtual void setCurveType(uint8_t v)             = 0;
    virtual void setNoteHead(uint8_t v)              = 0;
    virtual void setRimRatioThreshold(uint16_t v)    = 0;
    virtual void setChokeThreshold(uint16_t v)       = 0;
    virtual void setChokeEnabled(bool v)             = 0;
    virtual uint8_t getNoteHead()    const           = 0;

    // Tier-2 Edrumulus params (added Stage 2a). Default no-op so engines that don't
    // use them need not implement them. Values are the fixed-point reals from
    // InputConfig (see Config.h for the ×10 / ×1 / ×100 convention).
    virtual void setPreScanTimeMs(uint16_t)          {}
    virtual void setFirstPeakDiffThreshDb(uint16_t)  {}
    virtual void setDecayLen1Ms(uint16_t)            {}
    virtual void setDecayGradFact1(uint16_t)         {}
    virtual void setDecayLen2Ms(uint16_t)            {}
    virtual void setDecayGradFact2(uint16_t)         {}
    virtual void setDecayLen3Ms(uint16_t)            {}
    virtual void setDecayGradFact3(uint16_t)         {}
    virtual void setDecayFactDb(uint16_t)            {}
    virtual void setMaskTimeDecayFactDb(uint16_t)    {}
    virtual void setDecayEstDelayMs(uint16_t)        {}
    virtual void setDecayEstLenMs(uint16_t)          {}
    virtual void setDecayEstFactDb(uint16_t)         {}
    virtual void setClipCompAmpmapStep(uint16_t)     {}

    virtual ~TriggerEngine() = default;
};
