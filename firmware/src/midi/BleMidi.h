#pragma once
#include <Arduino.h>

void bleMidiInit();
void bleMidiPoll();
void bleMidiSendSysEx(const uint8_t* data, size_t len);
bool bleMidiIsConnected();
