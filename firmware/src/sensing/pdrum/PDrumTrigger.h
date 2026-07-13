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
  bool hitCrossStick;   // DUAL_PIEZO: rim-only cross-stick (replaces the rim note)
  bool hitAlt;          // PIEZO_SWITCH_CHOKE: alternate note (replaces the head note)
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

  // ---- Secondary trigger behaviours v1 (2026-07-12; telnet-`w` only, NOT in SysEx).
  // PART A (DUAL_PIEZO snare rim): rim gets its OWN threshold/sensitivity/curve —
  // previously it shared head's scale, a real defect since the two piezos see
  // genuinely different mechanical energy for the same strike. rimRatioThreshold
  // above is now UNUSED by padType==0 (left in place, not repurposed, per the task).
  uint16_t rimThreshold;        // DUAL: rim-channel fire threshold (raw ADC)
  uint16_t rimSensitivity;      // DUAL: rim upper ADC bound for velocity scaling
  byte     rimCurve;            // DUAL: rim velocity curve (same enum as curvetype)
  byte     crossStickNote;      // DUAL: note for a soft (cross-stick) rim hit
  byte     crossStickCutoff;    // DUAL: MIDI VELOCITY (0-127, NOT raw ADC — deliberate)
                                // below which a rim-won hit is a cross-stick, not a
                                // normal rim note. Compared vs the CURVED rim velocity.
  // PART B (PIEZO_SWITCH_CHOKE alternate note): concurrent with (not a toggle vs)
  // choke. chokeThreshold above is REUSED as the switch level for both.
  byte     alternateNote;       // note sent instead of head note on a coincident edge hit
  uint16_t minAltNoteVelocity;  // min head peak (raw ADC) to qualify as an alt-note hit
  uint16_t chokeHoldMs;         // choke sustain time (ms); was the kChokeHoldMs constant
  uint16_t chokeReleaseGraceMs; // choke release debounce (ms): a dip below chokeThreshold
                                // shorter than this does NOT reset the hold accumulator

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
  bool hasHitCrossStick()  const override { return hitCrossStick; }
  bool hasHitAlt()         const override { return hitAlt; }
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

  // TEMP DIAGNOSTIC (runaway investigation).
  int   getDebugLoopTimes()    const override { return loopTimes; }
  float getDebugPiezoAfterDc() const override { return (float)lastPiezoAfterDc_; }
  float getDebugDcOffsetHead() const override { return dcOffsetHead_; }
  int   getDebugSpikeRejects() const override { return spikeRejectCount_; }

  // TEMP DIAGNOSTIC (retrigger-cancel v2): live state-machine snapshot so the
  // whole IDLE→SCAN→MASK→MONITOR cycle can be watched on [HIT]/[RIM] prints.
  //   rcState: 0=IDLE 1=SCAN 2=MASK 3=MONITOR (see RcState below).
  //   lastConfirmedPeak: the reference the new-strike bar is measured against.
  //   refRising: reference-tracking sub-state (true=RISING, false=FALLING).
  int   getDebugRcState()           const override { return (int)rcState_; }
  int   getDebugLastConfirmedPeak() const override { return lastConfirmedPeak_; }
  bool  getDebugRefRising()         const override { return rcTracker_.dir == ExtremumTracker::DIR_RISING; }

  // TEMP DIAGNOSTIC (confirmation-based Scan v3): last-commit detail. A Scan commit
  // always produces a [HIT]/[RIM], so these ride the existing hit print (no separate
  // latch needed). scanExit: 0=SETTLE (adaptive) 1=HARDCAP (backstop).
  int   getDebugScanExit()     const override { return (int)scanLastExit_; }
  int   getDebugScanConfirms() const override { return scanLastConfirms_; }
  int   getDebugScanDurMs()    const override { return (int)scanLastDurMs_; }
  // DUAL_PIEZO rim/head ratio (rimBest*100/headBest) at the last commit, but ONLY
  // when both channels cleared their own threshold (the ambiguous both-fired case
  // where ratio actually decides the note); -1 sentinel otherwise. Lets rimRatioThreshold
  // keep being tuned from real captures.
  int   getDebugScanRatio()    const override { return scanLastRatio_; }

  // TEMP DIAGNOSTIC (retrigger-cancel v2 event visibility): monitor exits are
  // momentary and would be missed between debug polls, so they're latched here
  // (mirrors hasChoke()/clearChoke()). getLastRetrigExit(): 0=NATURAL 1=HARDCAP
  // 2=NEWSTRIKE (see RcExit). Lcp/DurMs are the values captured at that exit.
  bool  hasRetrigEvent()                 override { return rcEventPending_; }
  void  clearRetrigEvent()               override { rcEventPending_ = false; }
  int   getLastRetrigExit()        const override { return (int)rcLastExit_; }
  int   getLastRetrigExitLcp()     const override { return rcLastExitLcp_; }
  int   getLastRetrigExitDurMs()   const override { return (int)rcLastExitDurMs_; }

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
  // Repurposed 'retrig' config field (v2): now the retrigger-cancel MARGIN in raw
  // ADC counts — the prominence a rise must clear to count as a genuine new peak/
  // trough (and as a new strike). Was v1's samples-to-peak cutoff.
  void setRetriggerTime(uint16_t v)      override { retriggerMargin_ = v; }
  // NEW v3 tunables (telnet-`w` only; not in SysEx). scantime is now Scan's hard-cap.
  void setScanMargin(uint16_t v)         override { scanMargin_   = v; }
  void setSettleWaitMs(uint16_t v)       override { settleWaitMs_ = v; }
  void setEmaAlphaPct(uint16_t v)        override { emaAlphaPct_  = v; }
  // Secondary trigger behaviours v1 (telnet-`w` only; not in SysEx).
  void setRimThreshold(uint16_t v)       override { rimThreshold       = v; }
  void setRimSensitivity(uint16_t v)     override { rimSensitivity     = v; }
  void setRimCurve(uint8_t v)            override { rimCurve           = v; }
  void setCrossStickNote(uint8_t v)      override { crossStickNote     = v; }
  void setCrossStickCutoff(uint8_t v)    override { crossStickCutoff   = v; }
  void setAlternateNote(uint8_t v)       override { alternateNote      = v; }
  void setMinAltNoteVelocity(uint16_t v) override { minAltNoteVelocity = v; }
  void setChokeHoldMs(uint16_t v)        override { chokeHoldMs        = v; }
  void setChokeReleaseGraceMs(uint16_t v) override { chokeReleaseGraceMs = v; }
  uint8_t getNoteHead()    const         override { return noteHead; }

private:
  // Per-sample detection core (unchanged HelloDrum Scan/Mask logic, plus the v2
  // retrigger-cancel MONITOR phase after Mask). currentAbsIndex is the
  // SampleStream-absolute index of this sample; recorded at the threshold crossing
  // so scope capture can locate the attack.
  void sensing(int piezoValue, int rimValue, uint32_t currentAbsIndex);

  // Begin a fresh Scan window from the current sample — the same initiation as an
  // ordinary threshold crossing (used both from IDLE and when the retrigger-cancel
  // MONITOR detects a genuine new strike). Rim/firstPeak seeding applies to
  // DUAL_PIEZO only; harmless no-op fields for the other pad types.
  void startScan(int piezoValue, int rimValue, uint32_t currentAbsIndex);

  // Confirmation-based Scan service (v3): one sample of the per-channel ratchet-UP
  // peak confirmation. Returns true when Scan has committed (settle or hard-cap) and
  // the caller must finalize the hit (read scanHeadBest_/scanRimBest_, curve, →MASK).
  bool serviceScan(int piezoValue, int rimValue);

  // MASK-phase service: hold until masktime elapses since scan end, then enter the
  // retrigger-cancel MONITOR phase. Mask behaviour itself is unchanged.
  void serviceMask(int piezoValue);

  // Retrigger-cancel MONITOR service (v2): one sample of (a) the unconditional
  // new-strike check and (b) the debounced FALLING/RISING reference tracker that
  // ratchets lastConfirmedPeak_ down as the strike decays. Handles the natural and
  // hard-cap exits to IDLE internally. Returns true iff a genuine new strike was
  // detected and the caller must start a fresh Scan window from this sample.
  bool serviceRetriggerMonitor(int piezoValue);

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
  // The hold time is now the per-input `chokeHoldMs` field (telnet-`w` tunable);
  // it was the static kChokeHoldMs=5 constant before Secondary Trigger Behaviours v1.
  bool          chokeAbove_      = false;  // switch currently above threshold
  unsigned long chokeAboveSince_ = 0;      // millis() when it first went above
  unsigned long belowSince_      = 0;      // millis() of a pending release dip (0 = none);
                                           // release-side debounce, see the choke block

  uint32_t Fs_ = 8000;   // stored only; timing is millis()-based (see initialize)

  int           loopTimes = 0;   // in-scan sample counter (diagnostic only now)
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

  // ---- Retrigger-cancel v2 (head channel; see project_state.md "Retrigger-Cancel
  // Design Spec (v2)"). A genuine 4th state distinct from ordinary IDLE/SCAN: the
  // MONITOR accept bar (lastConfirmedPeak_ + margin) differs from IDLE's plain
  // headThreshold, so it cannot be represented as IDLE-with-different-data.
  enum RcState : uint8_t {
    RC_IDLE    = 0,   // waiting for a threshold crossing (accept bar = headThreshold)
    RC_SCAN    = 1,   // scan window open (running max → velocity), unchanged
    RC_MASK    = 2,   // flat blackout window after scan, unchanged
    RC_MONITOR = 3    // retrigger-cancel (accept bar = lastConfirmedPeak_ + margin)
  };
  enum RcExit     : uint8_t { RC_EXIT_NATURAL = 0, RC_EXIT_HARDCAP = 1, RC_EXIT_NEWSTRIKE = 2 };

  // Shared margin-debounced local-extremum tracker (v3). Extracted verbatim from the
  // v2 MONITOR reference tracker so BOTH mechanisms use ONE implementation, differing
  // only in what they do with a confirmed extremum:
  //   MONITOR — ratchet lastConfirmedPeak_ DOWN on each confirmed peak (decay).
  //   Scan    — ratchet the best peak UP toward the true peak (attack).
  // Alternates FALLING (track running min; a rise of > margin above it confirms a
  // TROUGH) and RISING (track running max; a fall of > margin below it confirms a
  // PEAK). feed() returns the event this sample confirmed; the confirmed value is
  // left in runMax (peak) / runMin (trough). Direction/action are the caller's.
  enum RefEvent : uint8_t { REF_NONE = 0, REF_PEAK = 1, REF_TROUGH = 2 };
  struct ExtremumTracker {
    // NOTE: enumerators are DIR_-prefixed because Arduino.h defines bare RISING /
    // FALLING as macros (attachInterrupt modes) — the same reason the v2 MONITOR
    // state used RC_FALLING/RC_RISING. Do not rename to bare FALLING/RISING.
    enum Dir : uint8_t { DIR_FALLING = 0, DIR_RISING = 1 };
    Dir dir    = DIR_FALLING;
    int runMin = 0;
    int runMax = 0;
    void reset(Dir d, int seed) { dir = d; runMin = seed; runMax = seed; }
    RefEvent feed(int sample, int margin) {
      if (dir == DIR_FALLING) {
        if (sample < runMin)               { runMin = sample; }
        else if (sample > runMin + margin) { dir = DIR_RISING;  runMax = sample; return REF_TROUGH; }
      } else {  // DIR_RISING
        if (sample > runMax)               { runMax = sample; }
        else if (sample < runMax - margin) { dir = DIR_FALLING; runMin = sample; return REF_PEAK; }
      }
      return REF_NONE;
    }
  };

  RcState    rcState_           = RC_IDLE;
  int        lastConfirmedPeak_ = 0;         // reference the new-strike bar rides on;
                                             // seeded to the scan's velocityRaw, then
                                             // ratcheted DOWN as the strike decays.
  ExtremumTracker rcTracker_;                // MONITOR's decay peak/trough tracker
  unsigned long rcMonitorStart_ = 0;         // millis() at MONITOR entry (hard-cap timer)

  // ---- Confirmation-based Scan (v3; see project_state.md "Scan Redesign + EMA
  // Smoothing"). Each channel ratchets its best peak UP via the shared tracker
  // above; the HEAD channel drives the settle/hard-cap exit, the rim channel
  // (DUAL_PIEZO only) is tracked concurrently so ratio discrimination is unchanged.
  enum ScanExit : uint8_t { SCAN_EXIT_SETTLE = 0, SCAN_EXIT_HARDCAP = 1 };
  ExtremumTracker scanHeadTrk_;
  ExtremumTracker scanRimTrk_;
  int  scanHeadBest_      = 0;         // best confirmed head peak this scan
  int  scanRimBest_       = 0;         // best confirmed rim peak this scan (DUAL)
  unsigned long scanSettleSince_ = 0;  // millis() of the last confirmed-peak event
  bool scanEverConfirmed_ = false;     // gate: no settle-exit before first confirmation
  int  scanConfirms_      = 0;         // confirmed peaks so far this scan (diagnostic)
  uint8_t       scanLastExit_     = SCAN_EXIT_SETTLE;  // exit path of the last commit
  unsigned long scanLastDurMs_    = 0;                 // duration (ms) of the last scan
  int           scanLastConfirms_ = 0;                 // confirmed peaks in the last scan
  int           scanLastRatio_    = -1;                // DUAL both-fired ratio, else -1

  // NEW telnet-`w`-only tunables (v3; NOT in the SysEx protocol — see spec). Member
  // defaults mirror Config.cpp; applyConfig() overwrites them from g_inputs at runtime.
  uint16_t scanMargin_   = 40;   // raw ADC counts: prominence to confirm a Scan peak
  uint16_t settleWaitMs_ = 5;    // ms with no new confirmed peak → commit (settle exit)
  uint16_t emaAlphaPct_  = 50;   // EMA alpha ×100 (0 or >=100 = smoothing disabled)

  // EMA smoothing state (v3): low-pass half of the approximate band-pass (DC-offset
  // removal is the high-pass half). Seeded once like the DC baseline; runs AFTER
  // spike-rejection so a large single-sample glitch is caught raw, not smeared first.
  float emaHead_   = 0.0f;
  float emaRim_    = 0.0f;
  bool  emaSeeded_ = false;

  // Repurposed 'retrig' config param (v2): the shared MONITOR margin, in raw ADC
  // counts. 0 = retrigger-cancel DISABLED for this input (explicit opt-out handled
  // in serviceMask()/serviceRetriggerMonitor(), not margin=0 arithmetic). Non-zero
  // needs real per-pad recorded-waveform calibration (open item). Default 0 (off)
  // matches Config.cpp; applyConfig() overwrites this from g_inputs at runtime.
  uint16_t retriggerMargin_ = 0;

  // Hard safety cap: force MONITOR→IDLE this long after entry regardless of decay,
  // for pads (e.g. KD-80 with a bouncy beater) whose lastConfirmedPeak_ may never
  // cleanly settle near headThreshold. Generous vs the ~600-700ms PDX-12 rebound
  // gaps measured so far. Non-user-facing backstop; millis() is fine at this scale.
  static constexpr unsigned long kRetrigHardCapMs = 900;

  // Seed cap (2026-07-08 fix): lastConfirmedPeak_ is seeded to min(velocityRaw,
  // kRetrigSeedCap) at scan end, NOT velocityRaw directly. If the initiating hit
  // clips the 12-bit ADC (~4095, normal on a firm-to-hard hit — confirmed on the
  // PD-7 at truepeak=4091), an uncapped seed makes the new-strike bar
  // (lastConfirmedPeak_ + margin) physically unreachable, so no strike however
  // hard can register for the whole MONITOR window. Capping below the ceiling
  // keeps that bar achievable, recovering the common case (a moderate-to-hard
  // second hit that does NOT itself clip). ROUGH PLACEHOLDER — comfortably above
  // the low-thousands amplitudes measured this week, with headroom below 4095 so
  // capValue+margin stays achievable; needs real per-pad recorded-waveform
  // calibration (same open item as margin). Does NOT solve clipped-vs-clipped
  // (both hits at ~4095 read identically) — an accepted, documented limitation,
  // not something a cap value can tune around. Non-user-facing.
  static constexpr int kRetrigSeedCap = 3500;

  // TEMP DIAGNOSTIC (retrigger-cancel v2 event latch): set when a MONITOR exit
  // fires, cleared once main reads it — same convention as chokeDetected/clearChoke().
  bool          rcEventPending_  = false;
  uint8_t       rcLastExit_      = RC_EXIT_NATURAL;
  int           rcLastExitLcp_   = 0;   // lastConfirmedPeak_ captured at that exit
  unsigned long rcLastExitDurMs_ = 0;   // MONITOR duration (ms) at that exit

  // TEMP DIAGNOSTIC (runaway investigation, remove once resolved): piezoValue
  // after DC-offset removal + spike rejection, captured every sensing() call.
  int lastPiezoAfterDc_ = 0;
  int spikeRejectCount_ = 0;   // cumulative count of the spike-reject branch firing
};


#endif
