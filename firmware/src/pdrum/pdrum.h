/*
  Based on
  "HELLO DRUM LIBRARY"

  by Ryo Kosaka

  GitHub : https://github.com/RyoKosaka/HelloDrum-arduino-Library
  Blog : https://open-e-drums.tumblr.com/
*/

#ifndef PDrum_h
#define PDrum_h

#define SPIKE_THRESHOLD 200

#include "Arduino.h"

class PDrum
{
public:
  PDrum(byte pin1, byte pin2);

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

  void sensing(int piezoValue, int rimValue, uint32_t currentRingHead = 0);

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
