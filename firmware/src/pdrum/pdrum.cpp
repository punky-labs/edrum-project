/*
  Based on
  "HELLO DRUM LIBRARY" 
  
  by Ryo Kosaka

  GitHub : https://github.com/RyoKosaka/HelloDrum-arduino-Library
  Blog : https://open-e-drums.tumblr.com/
*/

/*
  PAD TYPES

  0 Single Piezo
  1 Dual Piezo
  2 Dual Cymbal
  3 Dual cable Ride (Three Zone)

*/


//#define DEBUG_DRUM //<-- uncomment this line to enable debug mode with Serial.




#include "pdrum.h"
#include "Arduino.h"

//Pad with a sensor.
PDrum::PDrum(byte pin1, byte pin2)
{
  pin_1 = pin1;
  pin_2 = pin2;
  type = 0;
  padname = 0;
  headSensitivity = 1000; //0
  headThreshold = 100;    //1
  scantime = 10;          //2
  masktime = 30;          //3
  rimSensitivity = 200;   //4
  rimThreshold = 30;      //5
  curvetype = 0;          //6
  noteHead = 38;          //7
  noteRim = 39;           //8
}

//
// 
//
void PDrum::sensing(int piezoValue, int rimValue)
{

  int Threshold = headThreshold;
  int Sensitivity = headSensitivity;
  int RimThreshold = rimThreshold;
  int RimSensitivity = rimSensitivity;

  //Serial.println(piezoValue);

  hit = false;
  hitRim = false;
  choke = false;

  //when the value > threshold
  if ((piezoValue > Threshold && loopTimes == 0) || (rimValue > RimThreshold && loopTimes == 0))
  {
    //Start the scan time
    time_hit = millis(); //check the time pad hitted

    //compare time to cancel retrigger
    if (time_hit - time_end < masktime){
      return; //Ignore the scan
    }
    else{
      velocity = piezoValue; //first peak
      velocityRim = rimValue;
      loopTimes = 1;         //start scan trigger
    }
  }

  //peak scan start
  if (loopTimes > 0){
    if (piezoValue > velocity){
      velocity = piezoValue;
    }
    if (rimValue > velocityRim){
      velocityRim = rimValue;
    }
    loopTimes++;

    //scan end
    if (millis() - time_hit >= scantime){
      time_end = millis();
      velocity = curve(velocity, headThreshold, headSensitivity, curvetype);
      velocityRim = curve(velocityRim, rimThreshold, rimSensitivity, curvetype);

      // Edge
      if ((velocity - velocityRim < RimSensitivity) && (velocityRim > RimThreshold)) {
        velocity = velocityRim;
        hitRim = true;
      }

      // Head
      else if (1) {
        hit = true;
      }
      
      // Choke
      else if (0) {
        choke = true;
      }
      loopTimes = 0; //reset loopTimes (ready for next sensing)
    }
  }
}


//
// List all parameters of a drum
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
  //Curve Type 0 : Linear
  if (curveType == 0){
    //
  }

  //Curve Type 1 : exp 1
  else if (curveType == 1){
    resF = (126 / (pow(1.02, 126) - 1)) * (pow(1.02, resF - 1) - 1) + 1; // 1.02
  }

  //Curve Type 2 : exp 2
  else if (curveType == 2){
    resF = (126 / (pow(1.05, 126) - 1)) * (pow(1.05, resF - 1) - 1) + 1; // 1.05
  }

  //Curve Type 3 : log 1
  else if (curveType == 3){
    resF = (126 / (pow(0.98, 126) - 1)) * (pow(0.98, resF - 1) - 1) + 1; // 0.98
  }

  //Curve Type 4 : log 2
  else if (curveType == 4){
    resF = (126 / (pow(0.95, 126) - 1)) * (pow(0.95, resF - 1) - 1) + 1; // 0.95
  }

  else
  {
    resF = 0;
  }
  byte res;
    res = (byte)round(resF);
    return res;

}


