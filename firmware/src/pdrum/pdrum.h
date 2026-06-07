/*
  "HELLO DRUM LIBRARY" Ver.0.7.7
  
  by Ryo Kosaka

  GitHub : https://github.com/RyoKosaka/PDrum-arduino-Library
  Blog : https://open-e-drums.tumblr.com/
*/

#ifndef PDrum_h
#define PDrum_h

#include "Arduino.h"

const static char *padtype[] = {
    "Single Piezo", //0
    "Dual Piezo",   //1
    "Dual Cymbal",   //2
};

const static char *instrumentName[] = {
    "Kick",       //0
    "Snare",      //1
    "HiHat",      //2
    "Tom 1",      //3
    "Tom 2",      //4
    "Tom 3",      //5
    "Tom 4",      //6
    "Ride",       //7
    "Crash 1",   //8
    "Crash 2",   //9
    "HH Pedal",   //10
};


class PDrum
{
public:
  PDrum(byte pin1, byte pin2);

  const char *getName();

  int velocity;
  int velocityRim;
  int velocityCup;
  byte pedalCC;

  //  int exValue;
  byte exTCRT = 0;
  byte exFSR = 0;
  bool hit;
  bool openHH = false;
  bool closeHH = false;
  bool hitRim;
  bool hitCup;
  bool choke;
  bool sensorFlag;
  bool moving;
  bool pedalVelocityFlag = false;
  bool pedalFlag = true;
  bool settingHHC = false;
  bool chokeFlag;

  byte value;

  byte noteHead;
  byte noteRim;
  byte noteCup;
  byte noteEdge;
  byte noteOpen;
  byte noteClose;
  byte noteOpenEdge;
  byte noteCloseEdge;
  byte noteCross;
  uint16_t headThreshold;
  byte threshold2;
  uint16_t scantime;
  uint16_t masktime;
  uint16_t headSensitivity;
  byte curvetype;
  uint16_t rimThreshold;
  uint16_t rimSensitivity;
  byte type;
  byte padname;
  byte pin_1;
  byte pin_2;

  void sensing(int piezoValue, int rimValue);

private:
  int piezoValue;
  int rimValue;
  int sensorValue;
  int TCRT;
  int fsr;
  int fsr_prev;
  int firstSensorValue;
  int lastSensorValue;
  int peakSensorValue;
  int loopTimes = 0;
  unsigned long time_hit;
  unsigned long time_end;
  unsigned long time_choke;
  unsigned long time_hit_pedal_1;
  unsigned long time_hit_pedal_2;

  int curve(int velocityRaw, int threshold, int sensRaw, byte curveType);
  

};


#endif
