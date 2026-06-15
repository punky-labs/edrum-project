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


#include "pdrum.h"
#include "Arduino.h"

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
PDrum::PDrum(byte pin1, byte pin2)
{
  pin_1 = pin1;
  pin_2 = pin2;
  padType            = 1;    // PIEZO_SWITCH_CHOKE
  headSensitivity    = 800;
  headThreshold      = 20;
  scantime           = 3;
  masktime           = 80;
  rimRatioThreshold  = 40;
  chokeThreshold     = 50;
  chokeEnabled       = true;
  chokeDetected      = false;
  chokeHoldSamples   = 0;
  firstPeakChannel   = 0;
  curvetype          = 0;
  noteHead           = 38;
  velocityRaw        = 0;
  velocityRimRaw     = 0;
  loopTimes          = 0;
  triggerSnap        = 0;
}

//
// Three sensing code paths, selected by padType.
//
void PDrum::sensing(int piezoValue, int rimValue, uint32_t currentRingHead)
{
  // Spike rejection — discard single-sample outliers (applied to both channels)
  if (abs(piezoValue - prevPiezoValue) > SPIKE_THRESHOLD &&
      abs(piezoValue - prevPrevPiezoValue) > SPIKE_THRESHOLD) {
      piezoValue = prevPiezoValue;
  }
  prevPrevPiezoValue = prevPiezoValue;
  prevPiezoValue     = piezoValue;

  if (abs(rimValue - prevRimValue) > SPIKE_THRESHOLD &&
      abs(rimValue - prevPrevRimValue) > SPIKE_THRESHOLD) {
      rimValue = prevRimValue;
  }
  prevPrevRimValue = prevRimValue;
  prevRimValue     = rimValue;

  // Per-call outputs reset every pass. chokeDetected is a latch cleared by Core 0.
  hit    = false;
  hitRim = false;
  choke  = false;

  // =========================================================================
  // DUAL_PIEZO — both channels velocity-sensitive; ratio discrimination
  // =========================================================================
  if (padType == 0) {
    // Only the head channel can initiate a scan.
    // Rim channel is read during scan for discrimination but never starts one.
    if (loopTimes == 0) {
      if (piezoValue > headThreshold) {
        if (millis() - time_end < masktime) return;
        time_hit = millis();
        velocity    = piezoValue;
        velocityRim = rimValue;
        firstPeakChannel = (rimValue > piezoValue) ? 1 : 0;
        loopTimes = 1;
        triggerSnap = currentRingHead;
      }
    }
    if (loopTimes > 0) {
      if (piezoValue > velocity)    velocity    = piezoValue;
      if (rimValue   > velocityRim) velocityRim = rimValue;
      loopTimes++;
      if (millis() - time_hit >= scantime) {
        time_end = millis();
        velocityRaw    = velocity;
        velocityRimRaw = velocityRim;
        // Ratio-based discrimination
        // ratio = rimPeak * 100 / headPeak (integer, avoids float)
        int ratio = (velocity > 0) ? (velocityRim * 100 / velocity) : 0;
        bool isRim = (ratio > (int)rimRatioThreshold) ||
                     (ratio > 80 && firstPeakChannel == 1);
        velocity    = curve(velocity,    headThreshold, headSensitivity, curvetype);
        velocityRim = curve(velocityRim, headThreshold, headSensitivity, curvetype);
        if (isRim) { hitRim = true; }
        else       { hit    = true; }
        loopTimes = 0;
      }
    }
  }

  // =========================================================================
  // PIEZO_SWITCH_CHOKE — head piezo (hit only) + rim switch (choke control)
  // =========================================================================
  else if (padType == 1) {
    // Head piezo: standard peak detection → hit only, no rim note
    if (loopTimes == 0) {
      if (piezoValue > headThreshold) {
        if (millis() - time_end >= masktime) {
          time_hit = millis();
          velocity = piezoValue;
          loopTimes = 1;
          triggerSnap = currentRingHead;
        }
      }
    }
    if (loopTimes > 0) {
      if (piezoValue > velocity) velocity = piezoValue;
      loopTimes++;
      if (millis() - time_hit >= scantime) {
        time_end = millis();
        velocityRaw = velocity;
        velocity    = curve(velocity, headThreshold, headSensitivity, curvetype);
        hit = true;
        loopTimes = 0;
      }
    }

    // Switch/choke monitoring — runs independently of hit scan.
    // Always monitor switch channel, even during mask window.
    if (chokeEnabled) {
      if (rimValue > (int)chokeThreshold) {
        chokeHoldSamples++;
        // ~5ms sustained at ~110us/sample = ~45 samples
        if (chokeHoldSamples >= 45) {
          chokeDetected = true;
          chokeHoldSamples = 0;
        }
      } else {
        chokeHoldSamples = 0;
      }
    }
  }

  // =========================================================================
  // SINGLE_PIEZO — head piezo only, no rim logic
  // =========================================================================
  else if (padType == 2) {
    if (loopTimes == 0) {
      if (piezoValue > headThreshold) {
        if (millis() - time_end < masktime) return;
        time_hit = millis();
        velocity  = piezoValue;
        loopTimes = 1;
        triggerSnap = currentRingHead;
      }
    }
    if (loopTimes > 0) {
      if (piezoValue > velocity) velocity = piezoValue;
      loopTimes++;
      if (millis() - time_hit >= scantime) {
        time_end    = millis();
        velocityRaw = velocity;
        velocity    = curve(velocity, headThreshold, headSensitivity, curvetype);
        hit = true;
        loopTimes = 0;
      }
    }
  }
}


//
// Map raw ADC peak to a 1-127 MIDI velocity through the selected curve.
//
int PDrum::curve(int velocityRaw, int threshold, int sensRaw, byte curveType)
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
