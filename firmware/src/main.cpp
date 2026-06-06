#include <Arduino.h>
#include "config/Config.h"

static const char* PAD_TYPE_NAMES[] = {
    "piezo",
    "piezo+rim",
    "rim-only",
    "hihat-cc",
    "hihat-sw",
    "bass-drum",
    "dual-piezo"
};

static void printConfig() {
    Serial.println("--- Input config ---");
    for (uint8_t i = 0; i < NUM_INPUTS; i++) {
        const InputConfig& c = g_inputs[i];
        const char* typeName = (c.padType < 7) ? PAD_TYPE_NAMES[c.padType] : "unknown";
        Serial.printf(
            "Input %d: type=%-10s thresh=%-5u curve=%u retrig=%-4ums xtalk=%u "
            "note=%3u ch=%u z2note=%3u z2ch=%u cc=%u ccch=%u\n",
            i, typeName,
            c.threshold, c.velocityCurve, c.retriggerTime, c.crosstalkGroup,
            c.midiNote, c.midiChannel,
            c.zone2MidiNote, c.zone2MidiChannel,
            c.ccNumber, c.ccChannel
        );
    }
    Serial.println("--------------------");
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("eDrum v0.1 — ready");
    configLoad();
    printConfig();
}

void loop() {
    printConfig();
    delay(2000);
}
