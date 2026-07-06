/*
  Based on
  "HELLO DRUM LIBRARY"

  by Ryo Kosaka

  GitHub : https://github.com/RyoKosaka/HelloDrum-arduino-Library
  Blog : https://open-e-drums.tumblr.com/

  Refactored from PDrum into PDrumTrigger implementing the TriggerEngine
  interface. The per-sample sensing algorithm is preserved exactly — the
  block-based processBlock() adapter loops it over the samples in a block,
  and a DC-offset removal front-end is added for the ESP32-S3's unipolar ADC.
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

  bool hit;
  bool hitRim;
  bool choke;

  // Pad-type sensing parameters. padType encoding matches the firmware-wide
  // "settled design" (project_state.md) used by Config.cpp / applyConfig() /
  // PDrum2Trigger: 0=DUAL_PIEZO, 1=PIEZO_SWITCH_CHOKE, 2=SINGLE_PIEZO.
  uint8_t  padType;
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
  // Fs is stored for reference only: this engine's scan/mask/choke timing is all
  // millis()-based, so it has no sample-rate dependency to derive.
  void initialize(uint32_t sampleRateHz) override;

  // Block adapter: runs the per-sample sensing() over the block's n samples,
  // tracking the absolute sample index so scope-capture (getTriggerSnap) works.
  void processBlock(const uint16_t* headBlock, const uint16_t* rimBlock,
                    uint16_t n, uint32_t blockStartAbsIndex) override;

  bool hasHit()            const override { return hit; }
  bool hasHitRim()         const override { return hitRim; }
  bool hasChoke()          const override { return chokeDetected; }
  void clearChoke()              override { chokeDetected = false; }

  int  getVelocity()       const override { return velocity; }
  int  getVelocityRim()    const override { return velocityRim; }
  int  getVelocityRaw()    const override { return velocityRaw; }
  int  getVelocityRimRaw() const override { return velocityRimRaw; }

  // Distance (samples) from the end of the most recent block back to the threshold
  // crossing of the hit reported this block, in absolute sample space — same
  // convention as PDrum2Trigger. main maps it: crossingAbs = blockEndAbs - snap.
  uint32_t getTriggerSnap() const override { return triggerBack_; }

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
  // Per-sample detection core (unchanged HelloDrum logic). currentAbsIndex is the
  // SampleStream-absolute index of this sample; recorded at the threshold crossing
  // so scope capture can locate the attack.
  void sensing(int piezoValue, int rimValue, uint32_t currentAbsIndex);

  int curve(int velocityRaw, int threshold, int sensRaw, byte curveType);

  // DC-offset removal for the ESP32-S3's unipolar internal ADC (rests at a positive
  // bias). Same slow one-pole IIR + seed-once discipline validated in PDrum2Trigger:
  // the baseline is seeded ONCE from the first real sample and then only tracked;
  // it is NEVER reset on config changes (see PDrum2Trigger.cpp resetState() comment
  // "runaway-bug fix" — reseeding snaps the baseline to one unrepresentative sample
  // and produces a stable fake signal the detector reports as continuous hits).
  static constexpr float kDcIirGamma         = 0.99975003124f; // exp(-1/4000), tau=0.5s @ 8kHz
  static constexpr float kDcIirOneMinusGamma = 1.0f - kDcIirGamma;
  float dcOffsetHead_ = 0.0f;
  float dcOffsetRim_  = 0.0f;
  bool  dcSeeded_     = false;   // seed once at true first-run; never reseed

  // Choke hold: fire when the switch has stayed above chokeThreshold for this long.
  // millis()-based (was a hardcoded ~45-sample count that assumed ~9kHz sampling).
  static constexpr unsigned long kChokeHoldMs = 5;
  bool          chokeAbove_      = false;  // switch currently above threshold
  unsigned long chokeAboveSince_ = 0;      // millis() when it first went above

  uint32_t Fs_ = 8000;   // stored only; timing is millis()-based (see initialize)

  int           loopTimes = 0;
  unsigned long time_hit;
  unsigned long time_end;

  uint8_t firstPeakChannel;    // 0=head, 1=rim — which crossed threshold first

  // Scope capture: absolute index of the threshold crossing (set in sensing()),
  // converted at block end to triggerBack_ = blockEndAbs - crossAbsIndex_.
  uint32_t crossAbsIndex_ = 0;
  uint32_t triggerBack_   = 0;

  int prevPiezoValue     = 0;
  int prevPrevPiezoValue = 0;
  int prevRimValue       = 0;
  int prevPrevRimValue   = 0;
};


#endif
