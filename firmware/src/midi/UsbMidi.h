#pragma once
#include <Arduino.h>

void usbMidiInit();
void usbMidiPoll();
void usbMidiSendSysEx(const uint8_t* data, size_t len);
bool usbMidiIsConnected();