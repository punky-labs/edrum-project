#include "BleMidi.h"
#include "SysEx.h"
#include <BLEMidi.h>

// The max22/ESP32-BLE-MIDI library has no SysEx support — receivePacket() returns
// "System common message, not implemented yet" on 0xF0. We work around this by
// replacing the BLE characteristic callback after begin() and parsing raw BLE MIDI
// packets directly. Sending uses the NimBLE characteristic notify API directly.

static const char* _SVC_UUID  = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
static const char* _CHAR_UUID = "7772e5db-3868-4112-a1a9-f2669d106bf3";

static NimBLECharacteristic* _pChar = nullptr;

#define SX_BUF 320
static uint8_t  _sxBuf[SX_BUF];
static uint16_t _sxLen    = 0;
static bool     _sxActive = false;

// Parse a raw BLE MIDI packet for SysEx only.
// SysEx body bytes are always 0x00-0x7F; timestamps and other status bytes
// are 0x80-0xFF. F0 and F7 are the only 0xF* bytes we act on.
static void _parsePacket(const uint8_t* data, size_t size) {
    for (size_t i = 1; i < size; i++) {   // byte 0 is the BLE MIDI header, skip it
        uint8_t b = data[i];
        if (b == 0xF0) {
            _sxLen = 0; _sxActive = true;
        } else if (b == 0xF7) {
            if (_sxActive) sysexParse(_sxBuf, _sxLen);
            _sxActive = false; _sxLen = 0;
        } else if (b < 0x80) {
            if (_sxActive && _sxLen < SX_BUF) _sxBuf[_sxLen++] = b;
        }
        // 0x80-0xEF and 0xF1-0xF6, 0xF8-0xFF: timestamps or non-SysEx status — skip
    }
}

class _SysExCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        std::string v = c->getValue();
        _parsePacket((const uint8_t*)v.c_str(), v.length());
    }
};
static _SysExCb _cb;

void bleMidiInit() {
    BLEMidiServer.begin("eDrum");
    Serial.printf("BLE MIDI: address %s\n", NimBLEDevice::getAddress().toString().c_str());

    // Locate the MIDI characteristic via NimBLE and replace the library's
    // callback. The library's own callback ignores all SysEx messages.
    NimBLEServer* srv = NimBLEDevice::getServer();
    if (srv) {
        NimBLEService* svc = srv->getServiceByUUID(_SVC_UUID);
        if (svc) {
            _pChar = svc->getCharacteristic(_CHAR_UUID);
            if (_pChar) {
                _pChar->setCallbacks(&_cb);
                Serial.println("BLE MIDI: SysEx characteristic hooked");
            } else {
                Serial.println("BLE MIDI: WARNING - SysEx characteristic not found");
            }
        }
    }

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    if (pAdv) {
        pAdv->start();
        Serial.println("BLE MIDI: advertising started");
    } else {
        Serial.println("BLE MIDI: WARNING - advertising object not found");
    }
}

void bleMidiPoll() {
    // BLE callbacks run in the NimBLE FreeRTOS task — nothing to poll here.
}

bool bleMidiIsConnected() {
    return BLEMidiServer.isConnected();
}

void bleMidiSendSysEx(const uint8_t* data, size_t len) {
    // data = [F0][body...][F7], len includes both framing bytes.
    // Build BLE MIDI packets: first packet is [H][TS][MIDI...], continuation
    // packets are [H][MIDI...]. BLE MTU floor = 20 bytes; F0/F7 are embedded
    // in the data stream and need no special timestamp treatment in practice.
    if (!BLEMidiServer.isConnected() || !_pChar || len < 2) return;

    const size_t MTU = 20;
    uint8_t pkt[MTU];
    size_t  offset = 0;
    bool    first  = true;

    while (offset < len) {
        auto     t   = millis();
        size_t   remaining = len - offset;
        size_t   pktLen;

        if (first) {
            pkt[0] = (uint8_t)(0x80u | ((t >> 7) & 0x3Fu));   // header
            pkt[1] = (uint8_t)(0x80u | (t & 0x7Fu));           // timestamp
            size_t chunk = remaining < (MTU - 2) ? remaining : (MTU - 2);
            memcpy(pkt + 2, data + offset, chunk);
            pktLen  = chunk + 2;
            offset += chunk;
            first   = false;
        } else {
            pkt[0] = (uint8_t)(0x80u | ((t >> 7) & 0x3Fu));   // header only
            size_t chunk = remaining < (MTU - 1) ? remaining : (MTU - 1);
            memcpy(pkt + 1, data + offset, chunk);
            pktLen  = chunk + 1;
            offset += chunk;
        }

        _pChar->setValue(pkt, pktLen);
        _pChar->notify();
    }
}
