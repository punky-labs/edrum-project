#include "UsbMidi.h"
#include "SysEx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

static uint8_t _sxBuf[320];
static uint16_t _sxLen = 0;
static bool _sxActive = false;

static void _midiTask(void* arg) {
    while (true) {
        if (tud_midi_available()) {
            uint8_t pkt[4];
            while (tud_midi_packet_read(pkt)) {
                uint8_t cin = pkt[0] & 0x0F;
                if (cin == 0x04 || cin == 0x05 || cin == 0x06 || cin == 0x07) {
                    // SysEx packets
                    uint8_t cnt = (cin == 0x04) ? 3 : (cin == 0x05) ? 1 : (cin == 0x06) ? 2 : 3;
                    for (int i = 1; i <= cnt && _sxLen < sizeof(_sxBuf); i++) {
                        uint8_t b = pkt[i];
                        if (b == 0xF0) { _sxLen = 0; _sxActive = true; }
                        else if (b == 0xF7) {
                            if (_sxActive) sysexParse(_sxBuf, _sxLen);
                            _sxActive = false; _sxLen = 0;
                        } else if (_sxActive) {
                            _sxBuf[_sxLen++] = b;
                        }
                    }
                }
            }
        }
        vTaskDelay(1);
    }
}

void usbMidiInit() {
    xTaskCreate(_midiTask, "midi_task", 4096, nullptr, 5, nullptr);
    Serial.println("USB MIDI: initialized");
}

void usbMidiPoll() {}

void usbMidiSendSysEx(const uint8_t* data, size_t len) {
    // data includes F0 and F7
    // Send as USB MIDI SysEx packets (CIN 0x04/0x05/0x06/0x07)
    size_t i = 0;
    while (i < len) {
        uint8_t pkt[4] = {0, 0, 0, 0};
        size_t remaining = len - i;
        if (remaining >= 3 && i + 3 < len) {
            pkt[0] = 0x04; // SysEx start/continue
            pkt[1] = data[i]; pkt[2] = data[i+1]; pkt[3] = data[i+2];
            i += 3;
        } else if (remaining == 1) {
            pkt[0] = 0x05;
            pkt[1] = data[i]; i++;
        } else if (remaining == 2) {
            pkt[0] = 0x06;
            pkt[1] = data[i]; pkt[2] = data[i+1]; i += 2;
        } else {
            pkt[0] = 0x07;
            pkt[1] = data[i]; pkt[2] = data[i+1]; pkt[3] = data[i+2]; i += 3;
        }
        tud_midi_packet_write(pkt);
    }
}

bool usbMidiIsConnected() {
    return tud_connected();
}