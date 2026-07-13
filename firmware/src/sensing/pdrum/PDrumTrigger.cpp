/*
  Based on
  "HELLO DRUM LIBRARY"

  by Ryo Kosaka

  GitHub : https://github.com/RyoKosaka/HelloDrum-arduino-Library
  Blog : https://open-e-drums.tumblr.com/
*/

/*
  PAD TYPES

  0 DUAL_PIEZO         — head piezo + rim piezo, ratio discrimination
  1 PIEZO_SWITCH_CHOKE — head piezo + rim switch used as choke control
  2 SINGLE_PIEZO       — head piezo only
*/


#include "PDrumTrigger.h"
#include "Arduino.h"
#include <math.h>   // lroundf, for DC-offset rounding

// Precomputed curve lookup tables [0..126] → [1..127]
// Generated from the original pow() formulae:
//   resF = (126/(base^126 - 1)) * (base^(idx) - 1) + 1
static const uint8_t kCurveExp1[127] = {   // curve 1 — Expressive (base 1.02)
      1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  5,  5,
      5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10, 10, 11,
     11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18,
     19, 20, 20, 21, 21, 22, 23, 23, 24, 25, 25, 26, 27, 28, 28, 29,
     30, 31, 32, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
     45, 46, 47, 48, 49, 51, 52, 53, 54, 56, 57, 58, 60, 61, 63, 64,
     65, 67, 69, 70, 72, 73, 75, 77, 79, 80, 82, 84, 86, 88, 90, 92,
     94, 96, 98,100,102,105,107,109,112,114,117,119,122,124,127,
};

static const uint8_t kCurveExp2[127] = {   // curve 2 — Sensitive (base 1.05)
      1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
      1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
      2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  3,
      4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6,  7,
      7,  7,  7,  8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13,
     14, 15, 15, 16, 17, 18, 19, 20, 21, 21, 23, 24, 25, 26, 27, 29,
     30, 31, 33, 35, 36, 38, 40, 42, 44, 46, 48, 51, 53, 56, 59, 61,
     65, 68, 71, 75, 78, 82, 86, 90, 95,100,105,110,115,121,127,
};

static const uint8_t kCurveLog1[127] = {   // curve 3 — Punchy (base 0.98)
      1,  4,  6,  9, 12, 14, 17, 19, 21, 24, 26, 28, 30, 33, 35, 37,
     39, 41, 43, 45, 46, 48, 50, 52, 54, 55, 57, 58, 60, 62, 63, 65,
     66, 68, 69, 70, 72, 73, 74, 76, 77, 78, 79, 80, 82, 83, 84, 85,
     86, 87, 88, 89, 90, 91, 92, 93, 94, 94, 95, 96, 97, 98, 99, 99,
    100,101,102,102,103,104,104,105,106,106,107,108,108,109,109,110,
    111,111,112,112,113,113,114,114,115,115,116,116,116,117,117,118,
    118,118,119,119,120,120,120,121,121,121,122,122,122,123,123,123,
    123,124,124,124,125,125,125,125,126,126,126,126,127,127,127,
};

static const uint8_t kCurveLog2[127] = {   // curve 4 — Aggressive (base 0.95)
      1,  7, 13, 19, 24, 30, 34, 39, 43, 48, 52, 55, 59, 62, 66, 69,
     72, 74, 77, 80, 82, 84, 86, 88, 90, 92, 94, 96, 97, 99,100,101,
    103,104,105,106,107,108,109,110,111,112,113,113,114,115,115,116,
    116,117,117,118,118,119,119,120,120,120,121,121,121,122,122,122,
    122,123,123,123,123,124,124,124,124,124,124,125,125,125,125,125,
    125,125,125,125,125,126,126,126,126,126,126,126,126,126,126,126,
    126,126,126,126,126,126,127,127,127,127,127,127,127,127,127,127,
    127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
};


//Pad with a sensor.
PDrumTrigger::PDrumTrigger(byte pin1, byte pin2)
{
  pin_1 = pin1;
  pin_2 = pin2;
  padType            = 1;    // PIEZO_SWITCH_CHOKE
  headSensitivity    = 800;
  headThreshold      = 20;
  scantime           = 30;   // v3: Scan HARD-CAP ms (was old fixed-window duration)
  masktime           = 80;
  rimRatioThreshold  = 70;   // rim/head % above which a both-fired hit classifies as rim
  chokeThreshold     = 50;
  chokeEnabled       = true;
  chokeDetected      = false;
  chokeAbove_        = false;
  firstPeakChannel   = 0;
  curvetype          = 0;
  noteHead           = 38;
  velocityRaw        = 0;
  velocityRimRaw     = 0;
  loopTimes          = 0;
  // Secondary trigger behaviours v1 defaults (overwritten by applyConfig()).
  rimThreshold       = 20;
  rimSensitivity     = 800;
  rimCurve           = 0;
  crossStickNote     = 37;   // GM side stick
  crossStickCutoff   = 25;   // MIDI velocity (0-127), NOT raw ADC
  alternateNote      = 53;   // GM ride bell (placeholder)
  minAltNoteVelocity = 200;
  chokeHoldMs        = 5;
  chokeReleaseGraceMs = 30;  // release-debounce placeholder (ms), unvalidated
}

// Fs is stored only — this engine's timing is millis()-based, so there is nothing
// sample-rate-dependent to derive. dcSeeded_ is intentionally left false (member
// initializer) so the DC baseline seeds once from the first real sample in
// processBlock(); do NOT seed/reset it here.
void PDrumTrigger::initialize(uint32_t sampleRateHz)
{
  Fs_ = sampleRateHz ? sampleRateHz : 8000;
}

// Block adapter: run the per-sample sensing() over the block, carrying the absolute
// sample index so the threshold-crossing snapshot lands in absolute space. Mirrors
// PDrum2Trigger's crossAbsIndex_ -> triggerBack_ conversion for scope capture.
void PDrumTrigger::processBlock(const uint16_t* headBlock, const uint16_t* rimBlock,
                                uint16_t n, uint32_t blockStartAbsIndex)
{
  // Per-block output latches reset ONCE here (not per-sample inside sensing()) so a
  // hit that fires mid-block is still visible to main after the whole block runs.
  // chokeDetected is a separate latch cleared by main via clearChoke().
  hit           = false;
  hitRim        = false;
  hitCrossStick = false;
  hitAlt        = false;
  choke         = false;

  if (!headBlock || n == 0) return;

  bool           firedThisBlock = false;
  const uint32_t blockEndAbs    = blockStartAbsIndex + n - 1;

  for (uint16_t j = 0; j < n; j++) {
    const uint32_t absIdx = blockStartAbsIndex + j;
    const int piezo = (int)headBlock[j];
    const int rim   = rimBlock ? (int)rimBlock[j] : 0;
    sensing(piezo, rim, absIdx);
    if (hit || hitRim || hitCrossStick) firedThisBlock = true;
  }

  if (firedThisBlock) {
    triggerBack_ = (blockEndAbs >= crossAbsIndex_) ? (blockEndAbs - crossAbsIndex_) : 0;
  }
}

//
// Three sensing code paths, selected by padType.
//
void PDrumTrigger::sensing(int piezoValue, int rimValue, uint32_t currentAbsIndex)
{
  // DC-offset removal (ESP32-S3 unipolar ADC rests at a positive bias). Slow one-pole
  // IIR baseline, subtracted from BOTH channels before any detection. Seed the
  // baseline ONCE from the first sample, then only track it — never reseed (see the
  // seed-once note in the header / PDrum2Trigger runaway-bug fix).
  const float rawHead = (float)piezoValue;
  const float rawRim  = (float)rimValue;
  if (!dcSeeded_) {
      dcOffsetHead_ = rawHead;
      dcOffsetRim_  = rawRim;
      dcSeeded_     = true;
  } else {
      dcOffsetHead_ = kDcIirGamma * dcOffsetHead_ + kDcIirOneMinusGamma * rawHead;
      dcOffsetRim_  = kDcIirGamma * dcOffsetRim_  + kDcIirOneMinusGamma * rawRim;
  }
  piezoValue = (int)lroundf(rawHead - dcOffsetHead_);
  rimValue   = (int)lroundf(rawRim  - dcOffsetRim_);

  // Spike rejection — discard single-sample outliers (applied to both channels).
  // FIX (confirmed via spikeRejectCount_ data during a real runaway): history
  // (prevPiezoValue/prevPrevPiezoValue) must always track the TRUE incoming
  // sample, never the substituted/rejected one. The old code stored the rejected
  // value into history, which self-poisons: once two consecutive real samples
  // exceed SPIKE_THRESHOLD from a frozen reference (trivial for a fast piezo
  // attack peaking in the thousands), every subsequent real sample keeps getting
  // compared against the same stale pair and rejected again, forever — confirmed
  // by watching spikerej climb continuously (every single sample) for the full
  // duration of a real runaway, only releasing when a sample's delta happened to
  // fall back under threshold by chance.
  const int trueIncomingPiezo = piezoValue;
  if (abs(piezoValue - prevPiezoValue) > SPIKE_THRESHOLD &&
      abs(piezoValue - prevPrevPiezoValue) > SPIKE_THRESHOLD) {
      piezoValue = prevPiezoValue;
      spikeRejectCount_++;   // TEMP DIAGNOSTIC (runaway investigation)
  }
  prevPrevPiezoValue = prevPiezoValue;
  prevPiezoValue     = trueIncomingPiezo;

  if (abs(rimValue - prevRimValue) > SPIKE_THRESHOLD &&
      abs(rimValue - prevPrevRimValue) > SPIKE_THRESHOLD) {
      rimValue = prevRimValue;
  }
  prevPrevRimValue = prevRimValue;
  prevRimValue     = (int)lroundf(rawRim - dcOffsetRim_);   // FIX: track true incoming sample, same as head

  // EMA smoothing (v3) — the low-pass half of an approximate band-pass (DC-offset
  // removal above is the high-pass half). Runs AFTER spike-rejection (per the spec's
  // "Pipeline order (decided)"): a large single-sample glitch is caught raw by
  // spike-rejection first, not smeared across samples by the EMA. Same raw 0-4095
  // domain — no rescaling, so thresh/sens/margins all stay meaningful. Seeded once
  // like the DC baseline. emaAlphaPct 0 or >=100 disables it (100 = alpha 1.0 =
  // identity; 0 = off) — passthrough, but state stays seeded for a smooth re-enable.
  if (emaAlphaPct_ > 0 && emaAlphaPct_ < 100) {
    const float a = (float)emaAlphaPct_ / 100.0f;
    if (!emaSeeded_) {
      emaHead_ = (float)piezoValue;
      emaRim_  = (float)rimValue;
      emaSeeded_ = true;
    } else {
      emaHead_ = a * (float)piezoValue + (1.0f - a) * emaHead_;
      emaRim_  = a * (float)rimValue   + (1.0f - a) * emaRim_;
    }
    piezoValue = (int)lroundf(emaHead_);
    rimValue   = (int)lroundf(emaRim_);
  } else {
    emaHead_ = (float)piezoValue;   // keep state current for a smooth re-enable
    emaRim_  = (float)rimValue;
    emaSeeded_ = true;
  }

  lastPiezoAfterDc_ = piezoValue;   // TEMP DIAGNOSTIC: post DC-offset + spike + EMA
                                    // (the actual value the detection blocks consume)

  // NOTE: hit/hitRim/choke are reset per-BLOCK in processBlock(), not here, so a hit
  // firing on any sample latches until main consumes it after the block.

  // =========================================================================
  // DUAL_PIEZO — ratio-based head/rim classification, then velocity-based cross-stick.
  // Two stages: (1) CLASSIFY whether rim wins at all — ratio when both zones fire, or
  // default-to-rim when only rim fired; (2) once rim has won, a SEPARATE velocity
  // check splits a soft rim hit (cross-stick note) from a hard one (normal rim note).
  // History: reverted 2026-07-13 from the brief v1 "independent layering" model to
  // ratio (real PDX-8 captures showed heavy head/rim bleed — head hits 19-33% rim/head,
  // rim hits 153-173% — so both zones clear their thresholds on almost any strike, and
  // layering fired a spurious second note nearly every hit). Then cross-stick itself
  // was redefined 2026-07-13: this pad's mechanical coupling defeats physical
  // cross-stick detection (genuine "stick across rim, zero head contact" attempts
  // still read headpk 519-683 — overlapping soft head hits, and ~89-109% ratio,
  // near-identical to hard rimshots at ~95-106%), so head-presence and ratio both fail
  // as discriminators. Cross-stick is now purely "rim won classification AND its curved
  // velocity is soft". KEPT from v1: the either-channel Scan start, rim's independent
  // velocity scaling. rimThreshold stays the prerequisite floor gate for "rim fired".
  // =========================================================================
  if (padType == 0) {
    // Scan starts when EITHER channel crosses its OWN threshold (ARCHITECTURAL
    // CORRECTION 2026-07-12, required for cross-stick to be detectable at all): a
    // real cross-stick may produce no meaningful head signal, so a head-only start
    // gate makes "head stays below threshold" structurally impossible. Both channels
    // are tracked throughout regardless of which one triggered the scan.
    if (rcState_ == RC_IDLE) {
      if (piezoValue > (int)headThreshold || rimValue > (int)rimThreshold)
        startScan(piezoValue, rimValue, currentAbsIndex);
    } else if (rcState_ == RC_SCAN) {
      if (serviceScan(piezoValue, rimValue)) {
        time_end = millis();
        // Committed peaks: best confirmed, floored by the tracker's current running
        // max so a still-rising unconfirmed peak at hard-cap is never undercounted.
        const int headBest = max(scanHeadBest_, scanHeadTrk_.runMax);
        const int rimBest  = max(scanRimBest_,  scanRimTrk_.runMax);
        velocityRaw    = headBest;
        velocityRimRaw = rimBest;
        const bool headFired = headBest > (int)headThreshold;
        const bool rimFired  = rimBest  > (int)rimThreshold;
        scanLastRatio_ = -1;   // diagnostic: only set below in the both-fired case
        // STAGE 1 — classification: does rim win at all? (ratio when both zones fired,
        // else default-to-rim when only rim fired). This decides rim-vs-head ONLY; the
        // cross-stick-vs-normal-rim split is a separate STAGE 2 check below.
        bool rimWins = false;
        if (rimFired && !headFired) {
          rimWins = true;   // no ambiguity — rim is the only thing that fired
        } else if (rimFired && headFired) {
          const int ratio = (headBest > 0) ? (rimBest * 100 / headBest) : 0;
          scanLastRatio_ = ratio;
          rimWins = ratio > (int)rimRatioThreshold;
          if (!rimWins) {
            velocity = curve(headBest, headThreshold, headSensitivity, curvetype);
            hit      = true;
          }
        } else if (headFired) {
          // Only head cleared its threshold — unambiguous, no ratio needed.
          velocity = curve(headBest, headThreshold, headSensitivity, curvetype);
          hit      = true;
        }
        // (neither-fired cannot occur: Scan only starts once one channel crosses its
        // own threshold, and best-so-far is >= that crossing value within the scan.)

        // STAGE 2 — rim won: split cross-stick vs normal rim by CURVED velocity.
        if (rimWins) {
          // Rim scales through its OWN threshold/sensitivity/curve (head and rim piezos
          // see different mechanical energy for the same strike).
          velocityRim = curve(rimBest, rimThreshold, rimSensitivity, rimCurve);
          // crossStickCutoff is in MIDI VELOCITY units (0-127), compared against the
          // CURVED rim velocity — DELIBERATELY not raw ADC like every other threshold
          // in this file (this pad's coupling makes a soft-velocity rule the only
          // workable cross-stick discriminator). Do NOT "correct" it to raw ADC.
          if (velocityRim < (int)crossStickCutoff) hitCrossStick = true;  // soft → cross-stick
          else                                     hitRim        = true;  // hard → normal rim
        }
        loopTimes = 0;
        // Retrigger-cancel: seed the reference to this scan's own head peak and hand
        // off to MASK → MONITOR (MONITOR watches the HEAD channel only, per v2).
        lastConfirmedPeak_ = min(velocityRaw, kRetrigSeedCap);  // seed cap (see .h)
        rcState_ = RC_MASK;
      }
    } else if (rcState_ == RC_MASK) {
      serviceMask(piezoValue);
    } else {  // RC_MONITOR
      if (serviceRetriggerMonitor(piezoValue))
        startScan(piezoValue, rimValue, currentAbsIndex);
    }
  }

  // =========================================================================
  // PIEZO_SWITCH_CHOKE — head piezo (hit only) + rim switch (choke control)
  // =========================================================================
  else if (padType == 1) {
    // Head piezo: confirmation-based Scan → hit (or alternate note), plus choke below
    if (rcState_ == RC_IDLE) {
      if (piezoValue > headThreshold) startScan(piezoValue, rimValue, currentAbsIndex);
    } else if (rcState_ == RC_SCAN) {
      if (serviceScan(piezoValue, rimValue)) {
        time_end = millis();
        const int headBest = max(scanHeadBest_, scanHeadTrk_.runMax);
        velocityRaw = headBest;
        velocity    = curve(velocityRaw, headThreshold, headSensitivity, curvetype);
        hit = true;
        // Alternate-note check (NEW, independent of and CONCURRENT with choke — not a
        // mode toggle). At this commit instant, if the switch is ALSO elevated right
        // now (instantaneous read — deliberately no freshness/staleness check for v1)
        // AND the head hit is hard enough, main sends alternateNote INSTEAD OF the
        // head note. Does not suppress or interact with choke, which keeps monitoring
        // in its own fully independent lane below and can fire immediately after.
        if (rimValue > (int)chokeThreshold && headBest > (int)minAltNoteVelocity)
          hitAlt = true;
        loopTimes = 0;
        lastConfirmedPeak_ = min(velocityRaw, kRetrigSeedCap);  // seed cap: keep the
                                                                // new-strike bar reachable when this hit clips the ADC
        rcState_ = RC_MASK;
      }
    } else if (rcState_ == RC_MASK) {
      serviceMask(piezoValue);
    } else {  // RC_MONITOR
      if (serviceRetriggerMonitor(piezoValue))
        startScan(piezoValue, rimValue, currentAbsIndex);
    }

    // Switch/choke monitoring — runs independently of hit scan (UNCHANGED by the
    // v1 alternate-note work above; they share chokeThreshold but are otherwise fully
    // independent lanes). Always monitor switch channel, even during mask window.
    // Fire when the switch has stayed above threshold for chokeHoldMs (millis-based,
    // sample-rate-independent; now a per-input tunable, was the kChokeHoldMs=5 const).
    //
    // RELEASE-SIDE DEBOUNCE (2026-07-14): a real hand on a physical switch is never
    // perfectly continuous above a fixed level (grip shifts, contact bounce), so the
    // old ZERO-tolerance reset ("dip one sample below → discard the whole hold-time
    // accumulation") made a realistic ~500ms hold unreachable and gave chokeHoldMs a
    // fragile 5-10ms sweet spot. Fix: a brief dip does NOT reset immediately — start a
    // release-pending timer, and only treat it as a genuine release once the dip itself
    // persists for chokeReleaseGraceMs. Any real contact within the grace window cancels
    // the pending release, and hold-time accumulation continues as if the dip never
    // happened. (Only the DIP behaviour changes here; the hold-time/chokeHoldMs logic
    // and the immediate re-arm after firing are deliberately untouched.)
    if (chokeEnabled) {
      if (rimValue > (int)chokeThreshold) {
        belowSince_ = 0;   // real contact cancels any pending release
        if (!chokeAbove_) {
          chokeAbove_      = true;
          chokeAboveSince_ = millis();
        } else if (millis() - chokeAboveSince_ >= (unsigned long)chokeHoldMs) {
          chokeDetected = true;
          chokeAbove_   = false;   // re-arm (matches old count-reset behaviour)
        }
      } else if (chokeAbove_) {
        if (belowSince_ == 0) {
          belowSince_ = millis();   // dip just started — hold-time accumulation continues
        } else if (millis() - belowSince_ >= (unsigned long)chokeReleaseGraceMs) {
          chokeAbove_ = false;      // dip persisted past the grace window → genuine release
          belowSince_ = 0;
        }
        // else: still within grace — chokeAboveSince_ untouched, accumulation continues
      }
      // (if !chokeAbove_ and below threshold: nothing to do; belowSince_ is already 0.)
    }
  }

  // =========================================================================
  // SINGLE_PIEZO — head piezo only, no rim logic
  // =========================================================================
  else if (padType == 2) {
    if (rcState_ == RC_IDLE) {
      if (piezoValue > headThreshold) startScan(piezoValue, rimValue, currentAbsIndex);
    } else if (rcState_ == RC_SCAN) {
      if (serviceScan(piezoValue, rimValue)) {
        time_end    = millis();
        velocityRaw = max(scanHeadBest_, scanHeadTrk_.runMax);
        velocity    = curve(velocityRaw, headThreshold, headSensitivity, curvetype);
        hit = true;
        loopTimes = 0;
        lastConfirmedPeak_ = min(velocityRaw, kRetrigSeedCap);  // seed cap: keep the
                                                                // new-strike bar reachable when this hit clips the ADC
        rcState_ = RC_MASK;
      }
    } else if (rcState_ == RC_MASK) {
      serviceMask(piezoValue);
    } else {  // RC_MONITOR
      if (serviceRetriggerMonitor(piezoValue))
        startScan(piezoValue, rimValue, currentAbsIndex);
    }
  }
}


// -----------------------------------------------------------------------------
// Retrigger-cancel v2 helpers (head channel). See project_state.md
// "Retrigger-Cancel Design Spec (v2)" for the full mechanism.
// -----------------------------------------------------------------------------

// Begin a fresh Scan window from the current sample. Identical initiation to an
// ordinary threshold crossing, so a genuine second strike is captured (and
// velocity-scaled) by its own Scan window rather than estimated by the monitor.
void PDrumTrigger::startScan(int piezoValue, int rimValue, uint32_t currentAbsIndex)
{
  time_hit       = millis();
  velocity       = piezoValue;
  loopTimes      = 1;
  crossAbsIndex_ = currentAbsIndex;
  // Confirmation-based Scan (v3): seed the head tracker + best from the crossing
  // sample; head drives the settle/hard-cap exit. Settle can't fire until the first
  // confirmation (scanEverConfirmed_), so a slow-developing attack is never cut short.
  scanHeadTrk_.reset(ExtremumTracker::DIR_RISING, piezoValue);
  scanHeadBest_      = piezoValue;
  scanSettleSince_   = time_hit;
  scanEverConfirmed_ = false;
  scanConfirms_      = 0;
  if (padType == 0) {   // DUAL_PIEZO also seeds rim tracking + first-peak channel
    velocityRim      = rimValue;
    firstPeakChannel = (rimValue > piezoValue) ? 1 : 0;
    scanRimTrk_.reset(ExtremumTracker::DIR_RISING, rimValue);
    scanRimBest_     = rimValue;
  }
  rcState_ = RC_SCAN;
}

// Confirmation-based Scan (v3), one sample. Mirror image of the MONITOR decay
// tracker: ratchet each channel's best peak UP via the shared ExtremumTracker. The
// HEAD channel's confirmed peaks drive the settle timer and thus the exit; the rim
// channel (DUAL_PIEZO only) is tracked concurrently, purely to feed ratio
// discrimination. Returns true when Scan commits (settle or hard-cap).
bool PDrumTrigger::serviceScan(int piezoValue, int rimValue)
{
  loopTimes++;   // diagnostic in-scan sample count

  // Head channel: a confirmed peak ratchets best UP and resets the settle window.
  // Keeping watching past the first confirmed peak is deliberate — a piezo's
  // characteristic 2nd/3rd peak can exceed the 1st (Edrumulus), so locking in on
  // first confirmation would sometimes report the smaller one.
  if (scanHeadTrk_.feed(piezoValue, (int)scanMargin_) == REF_PEAK) {
    scanEverConfirmed_ = true;
    scanConfirms_++;
    if (scanHeadTrk_.runMax > scanHeadBest_) scanHeadBest_ = scanHeadTrk_.runMax;
    scanSettleSince_ = millis();   // any confirmed peak resets the settle window
  }

  // Rim channel (DUAL_PIEZO only): concurrent confirmation tracker, no exit role.
  if (padType == 0) {
    if (scanRimTrk_.feed(rimValue, (int)scanMargin_) == REF_PEAK &&
        scanRimTrk_.runMax > scanRimBest_) {
      scanRimBest_ = scanRimTrk_.runMax;
    }
  }

  // Exit: settle (adaptive — only once at least one peak is confirmed) or the
  // hard-cap backstop (repurposed `scantime` ms; generous vs the slowest real
  // attack measured so far — the PDX-8's ~5ms — so it only ever catches a
  // pathological signal, never a legitimate hit).
  const bool settle  = scanEverConfirmed_ &&
                       (millis() - scanSettleSince_ >= (unsigned long)settleWaitMs_);
  const bool hardcap = (millis() - time_hit >= (unsigned long)scantime);
  if (settle || hardcap) {
    scanLastExit_     = settle ? SCAN_EXIT_SETTLE : SCAN_EXIT_HARDCAP;
    scanLastDurMs_    = millis() - time_hit;
    scanLastConfirms_ = scanConfirms_;
    return true;
  }
  return false;
}

// MASK phase: unchanged blackout window. When masktime has elapsed since scan end,
// hand off to the retrigger-cancel MONITOR, seeding the reference tracker's running
// minimum from the current (decaying) sample.
void PDrumTrigger::serviceMask(int piezoValue)
{
  if (millis() - time_end >= masktime) {
    // retrig=0 means retrigger-cancel is DISABLED for this input (an explicit
    // opt-out, NOT margin=0 — plugging 0 into the formulas would make it maximally
    // aggressive, the opposite of off). Skip MONITOR entirely and return straight
    // to IDLE: exactly the pre-v2 Scan→Mask→Idle behaviour. Most pads (PD-7, KD-80)
    // don't need this feature; it's opt-in per pad (mesh-type). See v2 spec.
    if (retriggerMargin_ == 0) {
      rcState_ = RC_IDLE;
      return;
    }
    rcState_        = RC_MONITOR;
    rcMonitorStart_ = millis();
    rcTracker_.reset(ExtremumTracker::DIR_FALLING, piezoValue);
    // lastConfirmedPeak_ was seeded to velocityRaw at scan end (continuity).
  }
}

// One MONITOR sample. Returns true iff a genuine new strike was detected (caller
// must then startScan() from this sample).
bool PDrumTrigger::serviceRetriggerMonitor(int piezoValue)
{
  // retrig=0 disables retrigger-cancel. This covers the live case: `w <i> retrig 0`
  // arriving WHILE this input is already mid-MONITOR from an earlier hit. Exit
  // immediately to IDLE (reusing the NATURAL exit reason — a dedicated "disabled"
  // reason isn't worth a new diagnostic code here). The mask-end hand-off in
  // serviceMask() handles the normal already-0-before-the-hit case.
  if (retriggerMargin_ == 0) {
    rcLastExit_      = RC_EXIT_NATURAL;
    rcLastExitLcp_   = lastConfirmedPeak_;
    rcLastExitDurMs_ = millis() - rcMonitorStart_;
    rcEventPending_  = true;
    rcState_         = RC_IDLE;
    return false;
  }

  // (a) New-strike check — fast, unconditional, no debounce. lastConfirmedPeak_ is
  // already a trusted reference, so clearing it by a full margin is decisive alone.
  if (piezoValue > lastConfirmedPeak_ + (int)retriggerMargin_) {
    rcLastExit_      = RC_EXIT_NEWSTRIKE;
    rcLastExitLcp_   = lastConfirmedPeak_;
    rcLastExitDurMs_ = millis() - rcMonitorStart_;
    rcEventPending_  = true;              // TEMP DIAGNOSTIC latch
    return true;                          // caller starts a fresh Scan window
  }

  // (b) Reference tracking — debounced peak/trough detection that ratchets
  // lastConfirmedPeak_ DOWN as the strike decays. Uses the shared ExtremumTracker
  // (same margin-debounced logic as before the v3 extraction; the only MONITOR
  // action is the downward ratchet on a confirmed peak). (a) never fired, so the
  // confirmed peak is <= lastConfirmedPeak_ — genuinely smaller.
  if (rcTracker_.feed(piezoValue, (int)retriggerMargin_) == REF_PEAK) {
    lastConfirmedPeak_ = rcTracker_.runMax;
  }

  // Natural exit: once the reference has decayed to within a margin of headThreshold,
  // the MONITOR bar (lastConfirmedPeak_ + margin) and plain threshold detection are
  // functionally equivalent — exit early rather than waiting out the hard cap.
  if (lastConfirmedPeak_ <= (int)headThreshold + (int)retriggerMargin_) {
    rcLastExit_      = RC_EXIT_NATURAL;
    rcLastExitLcp_   = lastConfirmedPeak_;
    rcLastExitDurMs_ = millis() - rcMonitorStart_;
    rcEventPending_  = true;
    rcState_         = RC_IDLE;
    return false;
  }

  // Hard safety cap: always return to IDLE eventually, even for bouncy pads whose
  // reference never cleanly settles near headThreshold (e.g. KD-80).
  if (millis() - rcMonitorStart_ >= kRetrigHardCapMs) {
    rcLastExit_      = RC_EXIT_HARDCAP;
    rcLastExitLcp_   = lastConfirmedPeak_;
    rcLastExitDurMs_ = millis() - rcMonitorStart_;
    rcEventPending_  = true;
    rcState_         = RC_IDLE;
    return false;
  }

  return false;
}


//
// Map raw ADC peak to a 1-127 MIDI velocity through the selected curve.
//
int PDrumTrigger::curve(int velocityRaw, int threshold, int sensRaw, byte curveType)
{
  float resF = map(velocityRaw, threshold, sensRaw, 1, 127);
  if (resF <= 1){
      resF = 1;
    }

    if (resF > 127){
      resF = 127;
    }
  // Curve 0: Natural (linear) — unchanged
  if (curveType == 0) {
    // linear, no transform
  }
  // Curve 1: Expressive (exp base 1.02 — soft bias)
  else if (curveType == 1) {
    int idx = constrain((int)resF - 1, 0, 126);
    resF = kCurveExp1[idx];
  }
  // Curve 2: Sensitive (exp base 1.05 — stronger soft bias)
  else if (curveType == 2) {
    int idx = constrain((int)resF - 1, 0, 126);
    resF = kCurveExp2[idx];
  }
  // Curve 3: Punchy (log base 0.98 — loud bias)
  else if (curveType == 3) {
    int idx = constrain((int)resF - 1, 0, 126);
    resF = kCurveLog1[idx];
  }
  // Curve 4: Aggressive (log base 0.95 — stronger loud bias)
  else if (curveType == 4) {
    int idx = constrain((int)resF - 1, 0, 126);
    resF = kCurveLog2[idx];
  }
  // Curve 5: Custom (reserved — treat as linear for now)
  else if (curveType == 5) {
    // reserved, fall through as linear
  }
  else {
    resF = 0;
  }
  byte res;
    res = (byte)round(resF);
    return res;

}
