#pragma once
#include <Arduino.h>

class TriggerEngine {
public:
    // Call once per sample loop with current head and rim ADC values
    virtual void sensing(int headVal, int rimVal, uint32_t ringHead = 0) = 0;

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

    // Scope support
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

    virtual ~TriggerEngine() = default;
};
