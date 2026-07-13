#include "DevLog.h"

#ifdef DEV_BUILD

#include <stdarg.h>
#include "DevWiFi.h"

DevLogClass DevLog;

void DevLogClass::begin(bool mirrorSerial) {
    mirrorSerial_ = mirrorSerial;
}

// Write a run of bytes to both sinks. Both the Serial mirror and the telnet
// client get '\n' expanded to CRLF — raw terminals (telnet clients AND the USB
// serial monitor) otherwise stairstep, printing each new line where the previous
// one ended instead of returning to column 0.
void DevLogClass::emit(const char* s, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n') {
            if (i > start) {
                if (mirrorSerial_) Serial.write((const uint8_t*)(s + start), i - start);
                if (devwifi.clientConnected()) devwifi.clientWrite((const uint8_t*)(s + start), i - start);
            }
            static const uint8_t crlf[2] = { '\r', '\n' };
            if (mirrorSerial_) Serial.write(crlf, 2);
            if (devwifi.clientConnected()) devwifi.clientWrite(crlf, 2);
            start = i + 1;
        }
    }
    if (start < len) {
        if (mirrorSerial_) Serial.write((const uint8_t*)(s + start), len - start);
        if (devwifi.clientConnected()) devwifi.clientWrite((const uint8_t*)(s + start), len - start);
    }
}

void DevLogClass::print(const char* s) {
    emit(s, strlen(s));
}

void DevLogClass::println(const char* s) {
    emit(s, strlen(s));
    emit("\n", 1);
}

void DevLogClass::printf(const char* fmt, ...) {
    // 256 was fine at bring-up but has been quietly too small for a while: both the
    // [HIT]/[RIM] debug lines (25+ fields now) and the `s` config dump (24+ fields
    // per input, after Secondary Trigger Behaviours v1) can exceed it, silently
    // truncating mid-line (see project_state.md, 2026-07-12 housekeeping) --
    // vsnprintf() cuts off before the trailing '\n', so the next print runs on
    // directly from wherever it got cut, looking like lost linebreaks. 512 gives
    // real headroom for both current worst cases and continued field growth.
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;  // truncated — emit what fit
    emit(buf, (size_t)n);
}

int DevLogClass::available() {
    return devwifi.clientAvailable();
}

int DevLogClass::read() {
    return devwifi.clientRead();
}

// Read up to '\n' with a short timeout so it can never stall the main loop
// (which must keep pumping the sample stream). Mirrors Serial.readStringUntil('\n')
// + trim() as the old 'o'/'w' handlers used.
String DevLogClass::readLine() {
    String out;
    uint32_t t0 = millis();
    while ((millis() - t0) < 20) {
        int c = devwifi.clientRead();
        if (c < 0) continue;               // nothing right now — spin until timeout
        if (c == '\n') break;
        out += (char)c;
        t0 = millis();                     // got a byte: extend the window
    }
    out.trim();
    return out;
}

#endif  // DEV_BUILD
