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

    // TEMP DIAGNOSTIC (mask-time-rescue runaway investigation, remove once resolved):
    // count of mask-time-rescue branch firings within the most recent processBlock()
    // call. Default 0 for engines that don't implement/need it.
    virtual int getRescueCount() const { return 0; }

    // TEMP DIAGNOSTIC (runaway investigation cont'd): raw state-machine numbers at
    // the moment of the most recent hit, so we can see the real comparison values
    // instead of reasoning about them abstractly. Defaults are sentinel (-1) for
    // engines that don't implement this.
    virtual float getDebugThreshold() const { return -1.0f; }
    virtual float getDebugPeakVal()   const { return -1.0f; }
    virtual int   getDebugMaskCnt()   const { return -1; }
    virtual int   getDebugDecayCnt()  const { return -1; }
    virtual int   getDebugDecayLen()  const { return -1; }

    // TEMP DIAGNOSTIC (DC-reseed runaway confirmation): how many times the DC
    // baseline has been (re)seeded since boot, and the raw sample value it was
    // last seeded from. Should be 1 (boot only) after the resetState() fix.
    virtual int   getDebugSeedCount()   const { return -1; }
    virtual float getDebugLastSeedRaw() const { return -1.0f; }

    // TEMP DIAGNOSTIC: how many times buildDerived() has actually run since boot.
    // Should be 1 (boot only) unless a config change legitimately requested it.
    virtual int   getDebugBuildCount()  const { return -1; }

    // TEMP DIAGNOSTIC: the actual decision-path values (never yet observed directly —
    // everything we've looked at so far, e.g. getDebugPeakVal(), is the raw/velocity
    // track, not this one). Reflects the last sample processed in the most recent
    // processBlock() call.
    virtual float getDebugXFilt()      const { return -1.0f; }
    virtual float getDebugXFiltDecay() const { return -1.0f; }

    // TEMP DIAGNOSTIC (PDrumTrigger runaway investigation): state-machine internals
    // for the HelloDrum-derived engine, since it shares none of the fields above.
    virtual int   getDebugLoopTimes()     const { return -1; }
    virtual float getDebugPiezoAfterDc()  const { return -1.0f; }
    virtual float getDebugDcOffsetHead()  const { return -1.0f; }
    virtual int   getDebugSpikeRejects()  const { return -1; }

    // TEMP DIAGNOSTIC (retrigger-cancel validation): count of hits rejected for a
    // too-slow attack (likely a mechanical rebound, not a genuine strike).
    virtual int   getDebugRetriggerRejects() const { return -1; }

    // TEMP DIAGNOSTIC (retrigger-cancel validation cont'd): per-event detail for
    // the most recent rejection, since rejections otherwise produce no visible
    // signal at all (no hit, no MIDI, nothing for main to print). Mirrors the
    // hasChoke()/clearChoke() latch pattern. Default no-op for other engines.
    virtual bool  hasReject()               { return false; }
    virtual void  clearReject()             {}
    virtual int   getLastRejectPeakIdx()    const { return -1; }
    virtual int   getLastRejectVelocityRaw() const { return -1; }
    virtual int   getLastPeakIdx()          const { return -1; }

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
    // Retrigger-cancel sensitivity (repurposed 'retrig' param, was dead code —
    // previously stored/shown but never applied). Default no-op so engines that
    // don't implement it (PDrum2Trigger) need no changes.
    virtual void setRetriggerTime(uint16_t)          {}
    virtual uint8_t getNoteHead()    const           = 0;

    // Force any deferred, config-dependent rebuild to happen NOW (synchronously),
    // rather than lazily on the next processBlock(). Call after applyConfig() while
    // ADC sampling is paused, so the (potentially slow) rebuild can't stall the
    // hot path. Default no-op for engines with nothing to rebuild.
    virtual void syncConfig()                        {}

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
