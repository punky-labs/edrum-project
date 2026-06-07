// -----------------------------------------------------------------------------
// PDrum - Punky Drum MIDI controller for Raspberry Pi Pico


#include <Adafruit_MCP3008.h>
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPixel.h>
#include <MIDI.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <vector>
#include <memory>
#include <cstring>
#include <algorithm>

#include <pdrum.h>
#include "midi_sysex.h"


Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

Adafruit_MCP3008 adc;

int Power = 11;
int PIN  = 12;
#define NUMPIXELS 1

Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Convert color enum to RGB values
uint32_t getColorRGB(LEDColor color) {
  switch(color) {
    case BLACK:   return pixels.Color(0, 0, 0);
    case RED:     return pixels.Color(255, 0, 0);
    case GREEN:   return pixels.Color(0, 255, 0);
    case BLUE:    return pixels.Color(0, 0, 255);
    case YELLOW:  return pixels.Color(255, 255, 0);
    case CYAN:    return pixels.Color(0, 255, 255);
    case MAGENTA: return pixels.Color(255, 0, 255);
    case WHITE:   return pixels.Color(255, 255, 255);
    case ORANGE:  return pixels.Color(255, 165, 0);
    case PURPLE:  return pixels.Color(128, 0, 128);
    case PINK:    return pixels.Color(255, 192, 203);
    default:      return pixels.Color(0, 0, 0);
  }
}

// Set LED to a color (non-blocking)
void setLED(LEDColor color) {
  pixels.setPixelColor(0, getColorRGB(color));
  pixels.show();
}

// Turn off the LED
void turnOffLED() {
  setLED(BLACK);
}

#define LOGLENGTH 150



// support up to 4 pads (dynamically instantiated from config)
#define MAX_PADS 4
PDrum* drums[MAX_PADS] = { nullptr, nullptr, nullptr, nullptr };
int padsCount = 0;

// helper: map pad index to ADC channels
inline int headChannelForPad(int i){ return i*2 + 1; }
inline int rimChannelForPad(int i){ return i*2 + 0; }

// Load configuration from LittleFS (/config.json) and instantiate `PDrum` objects
void loadConfig(){
  if (!LittleFS.begin()){
    Serial.println("[W] LittleFS not mounted - using default single pad");
    setLED(RED);
    // fallback to single pad
    padsCount = 1;
    drums[0] = new PDrum(rimChannelForPad(0), headChannelForPad(0));
    return;
  }

  File f = LittleFS.open("/config.json","r");
  if(!f){
    Serial.println("[W] /config.json not found on LittleFS - using default single pad");
    setLED(RED);
    padsCount = 1;
    drums[0] = new PDrum(rimChannelForPad(0), headChannelForPad(0));
    return;
  }

  StaticJsonDocument<8192> doc;
  auto err = deserializeJson(doc, f);
  f.close();
  if(err){
    Serial.print("[E] JSON parse error: ");
    Serial.println(err.c_str());
    padsCount = 1;
    drums[0] = new PDrum(rimChannelForPad(0), headChannelForPad(0));
    return;
  }

  JsonArray kit = doc["kit"].as<JsonArray>();
  padsCount = 0;
  for(JsonObject item : kit){
    if(padsCount >= MAX_PADS) break;
    int i = padsCount;
    int headCh = headChannelForPad(i);
    int rimCh = rimChannelForPad(i);
    // instantiate with channel placeholders (constructor expects two pins)
    drums[i] = new PDrum(rimCh, headCh);

    // Apply configuration values when present
    if(item.containsKey("headSensitivity")) drums[i]->headSensitivity = (byte)item["headSensitivity"];
    if(item.containsKey("headThreshold")) drums[i]->headThreshold = (byte)item["headThreshold"];
    if(item.containsKey("scantime")) drums[i]->scantime = (byte)item["scantime"];
    if(item.containsKey("masktime")) drums[i]->masktime = (byte)item["masktime"];
    if(item.containsKey("rimSensitivity")) drums[i]->rimSensitivity = (byte)item["rimSensitivity"];
    if(item.containsKey("rimThreshold")) drums[i]->rimThreshold = (byte)item["rimThreshold"];
    if(item.containsKey("curvetype")) drums[i]->curvetype = (byte)item["curvetype"];
    if(item.containsKey("noteHead")) drums[i]->noteHead = (byte)item["noteHead"];
    if(item.containsKey("noteRim")) drums[i]->noteRim = (byte)item["noteRim"];

    // store pin/channel mapping for reference
    drums[i]->pin_1 = rimCh;
    drums[i]->pin_2 = headCh;

    padsCount++;
    setLED(GREEN);
  }

  if(padsCount == 0){
    padsCount = 1;
    drums[0] = new PDrum(rimChannelForPad(0), headChannelForPad(0));
  }

  Serial.print("[I] Loaded "); Serial.print(padsCount); Serial.println(" pads from config.json");
}

//
//
void playNote(byte note, byte velocity){  //(note, velocity)
  MIDI.sendNoteOn(note, velocity, 10); //(note, velocity, channel)
  MIDI.sendNoteOff(note, 0, 10);
  Serial.print("Note On: ");
  Serial.print(note); 
  Serial.print(" Velocity: ");
  Serial.println(velocity); 

}

// send SysEx helper (payload excludes 0xF0/0xF7)
void sendSysExPayload(const uint8_t *payload, uint8_t len){
  MIDI.sendSysEx(len, payload, true);
}

//
//
int pinVals[8];
#define SMOOTHING 0.1
#define FLOORVAL 0
//
int smoothVals(int pinVal, int reading){
  if(reading<FLOORVAL) reading = 0;
  return SMOOTHING * pinVal + (1.0 - SMOOTHING) * reading;
}

//
//
int max_val = 0;
void listRAW(){
  Serial.print(max_val);
  for(int i=0;i<8;i++){
    if(pinVals[i]>max_val) max_val = pinVals[i];
    Serial.print("\t");
    Serial.print(pinVals[i]);
  }
  Serial.println();
}

/*
//Pad with a sensor.
PDrum::PDrum()
{
  pin_1 = -1;
  pin_2 = -1;
  type = 0;
  padname = 0;
  headSensitivity = 100;   //0
  headThreshold = 10;     //1
  scantime = 10;       //2
  masktime = 30;       //3
  rimSensitivity = 20; //4 edgeThreshold
  rimThreshold = 3;    //5 cupThreshold
  curvetype = 0;       //6
  noteHead = 38;           //7
  noteRim = 39;        //8
  noteCup = 40;        //9
}*/



// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void setup() {

  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  usb_midi.setStringDescriptor("PunkyDrum MIDI");

  // Initialize MIDI, and listen to all MIDI channels
  // This will also call usb_midi's begin()
  MIDI.begin();
  MIDI.setHandleSystemExclusive(OnMidiSysEx);

  Serial.begin(115200);
  pixels.begin();
  pinMode(Power,OUTPUT);
  digitalWrite(Power, HIGH);
  setLED(ORANGE);
  delay(10000);
  setLED(BLUE);
  Serial.println("[I] Booted.");
  // Load pad configuration from LittleFS
  loadConfig();

  

}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void setup1() {
  adc.begin(2,3,4,1);
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void loop() {
  // Check for incoming MIDI messages (including SysEx)
  MIDI.read();
  
  //listRAW();
  // iterate all configured pads
  for(int i=0;i<padsCount;i++){
    int headCh = headChannelForPad(i);
    int rimCh = rimChannelForPad(i);
    if(!drums[i]) continue;
    drums[i]->sensing(pinVals[headCh], pinVals[rimCh]);

    if(drums[i]->hit){
      playNote(drums[i]->noteHead, drums[i]->velocity);
    }
    else if(drums[i]->hitRim){
      playNote(drums[i]->noteRim, drums[i]->velocity);
    }
  }

}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void loop1(){
   for (int chan=0; chan<8; chan++) {
    pinVals[chan] = smoothVals(pinVals[chan], adc.readADC(chan));
  }
}

