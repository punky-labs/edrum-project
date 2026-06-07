#include <Arduino.h>
#include <Adafruit_MCP3008.h>
#include <Adafruit_NeoPixel.h>
#include <MIDI.h>

#include <Adafruit_TinyUSB.h>

Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

int Power = 11;
int PIN  = 12;
#define NUMPIXELS 1

Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);


void setup() {
    Serial.begin(115200);
    usb_midi.setStringDescriptor("PunkyDrum MIDI");
    usb_midi.begin();
    MIDI.begin(MIDI_CHANNEL_OMNI);
    while (!TinyUSBDevice.mounted()) delay(1);
    Serial.println("RP2040 MIDI ready");
}

void loop() {
    MIDI.read();
    // Send a test note every 2 seconds
    static uint32_t last = 0;
    if (millis() - last > 2000) {
        last = millis();
        MIDI.sendNoteOn(60, 100, 1);
        delay(100);
        MIDI.sendNoteOff(60, 0, 1);
        Serial.println("Note sent");
    }
}