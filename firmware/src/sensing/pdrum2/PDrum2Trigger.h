#pragma once
#include "../TriggerEngine.h"

// PDrum2Trigger — next-generation sensing engine (not yet implemented)
// Extends TriggerEngine. Drop-in replacement for PDrumTrigger.
class PDrum2Trigger : public TriggerEngine {
public:
    PDrum2Trigger(byte headCh, byte rimCh);

    void sensing(int headVal, int rimVal, uint32_t ringHead = 0) override {}

    bool hasHit()            const override { return false; }
    bool hasHitRim()         const override { return false; }
    bool hasChoke()          const override { return false; }
    void clearChoke()              override {}
    int  getVelocity()       const override { return 0; }
    int  getVelocityRim()    const override { return 0; }
    int  getVelocityRaw()    const override { return 0; }
    int  getVelocityRimRaw() const override { return 0; }
    uint32_t getTriggerSnap() const override { return 0; }
    void setPadType(uint8_t)              override {}
    void setHeadThreshold(uint16_t)       override {}
    void setHeadSensitivity(uint16_t)     override {}
    void setScanTime(uint16_t)            override {}
    void setMaskTime(uint16_t)            override {}
    void setCurveType(uint8_t)            override {}
    void setNoteHead(uint8_t)             override {}
    void setRimRatioThreshold(uint16_t)   override {}
    void setChokeThreshold(uint16_t)      override {}
    void setChokeEnabled(bool)            override {}
    uint8_t getNoteHead() const           override { return 0; }
};
