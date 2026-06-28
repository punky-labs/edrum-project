/*
  Based on
  "HELLO DRUM LIBRARY"

  by Ryo Kosaka

  GitHub : https://github.com/RyoKosaka/HelloDrum-arduino-Library
  Blog : https://open-e-drums.tumblr.com/

  Refactored from PDrum into PDrumTrigger implementing the TriggerEngine
  interface. The sensing algorithm is preserved exactly — only the class
  name and the TriggerEngine accessor wrappers are added.
*/

#ifndef PDrumTrigger_h
#define PDrumTrigger_h

#ifndef SPIKE_THRESHOLD
#define SPIKE_THRESHOLD 200
#endif

#include "Arduino.h"
#include "../TriggerEngine.h"

class PDrumTrigger : public TriggerEngine
{
public:
  PDrumTrigger(byte pin1, byte pin2);

  int velocity;
  int velocityRim;
  int velocityRaw;     // pre-curve head velocity (ADC units, 0-1023)
  int velocityRimRaw;  // pre-curve rim velocity (ADC units, 0-1023)
  uint32_t triggerSnap = 0;  // ringHead value at threshold crossing — for scope capture

  bool hit;
  bool hitRim;
  bool choke;

  // Pad-type sensing parameters
  uint8_t  padType;             // 0=DUAL_PIEZO, 1=PIEZO_SWITCH_CHOKE, 2=SINGLE_PIEZO
  uint16_t rimRatioThreshold;   // DUAL_PIEZO: ratio*100 threshold
  uint16_t chokeThreshold;      // PIEZO_SWITCH_CHOKE: ADC switch threshold
  bool     chokeEnabled;        // PIEZO_SWITCH_CHOKE: enable choke
  bool     chokeDetected;       // set true when choke confirmed — Core 0 reads and clears

  byte     noteHead;
  uint16_t headThreshold;
  uint16_t scantime;
  uint16_t masktime;
  uint16_t headSensitivity;
  byte     curvetype;
  byte     pin_1;
  byte     pin_2;

  // ----- TriggerEngine interface -----
  void sensing(int piezoValue, int rimValue, uint32_t currentRingHead = 0) override;

  bool hasHit()            const override { return hit; }
  bool hasHitRim()         const override { return hitRim; }
  bool hasChoke()          const override { return chokeDetected; }
  void clearChoke()              override { chokeDetected = false; }

  int  getVelocity()       const override { return velocity; }
  int  getVelocityRim()    const override { return velocityRim; }
  int  getVelocityRaw()    const override { return velocityRaw; }
  int  getVelocityRimRaw() const override { return velocityRimRaw; }

  uint32_t getTriggerSnap() const override { return triggerSnap; }

  void setPadType(uint8_t t)             override { padType           = t; }
  void setHeadThreshold(uint16_t v)      override { headThreshold     = v; }
  void setHeadSensitivity(uint16_t v)    override { headSensitivity   = v; }
  void setScanTime(uint16_t v)           override { scantime          = v; }
  void setMaskTime(uint16_t v)           override { masktime          = v; }
  void setCurveType(uint8_t v)           override { curvetype         = v; }
  void setNoteHead(uint8_t v)            override { noteHead          = v; }
  void setRimRatioThreshold(uint16_t v)  override { rimRatioThreshold = v; }
  void setChokeThreshold(uint16_t v)     override { chokeThreshold    = v; }
  void setChokeEnabled(bool v)           override { chokeEnabled      = v; }
  uint8_t getNoteHead()    const         override { return noteHead; }

private:
  int           loopTimes = 0;
  unsigned long time_hit;
  unsigned long time_end;

  uint8_t firstPeakChannel;    // 0=head, 1=rim — which crossed threshold first
  uint8_t chokeHoldSamples;    // consecutive samples switch has been above chokeThreshold

  int curve(int velocityRaw, int threshold, int sensRaw, byte curveType);

  int prevPiezoValue     = 0;
  int prevPrevPiezoValue = 0;
  int prevRimValue       = 0;
  int prevPrevRimValue   = 0;
};


#endif
