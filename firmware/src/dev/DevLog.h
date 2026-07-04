// DevLog — the single seam for firmware DEBUG I/O (dev-build only).
//
// The rest of the firmware calls DevLog instead of Serial for debug output and
// command input. The active sink is the telnet client owned by DevWiFi; output
// MAY also mirror to USB Serial TX (log_mirror_serial, default on) which is handy
// during bring-up. If no telnet client is connected, output is dropped and input
// reports "nothing available" — DevLog NEVER blocks.
//
// This is NOT for MIDI, and NOT for the early-boot lines (those stay on Serial —
// TX works pre-WiFi and doesn't disturb MIDI, which isn't active yet).
//
// Instance is named `DevLog` (Arduino Serial-style) so call sites read
// DevLog.printf(...) / DevLog.available() / DevLog.readLine().
#pragma once
#include <Arduino.h>

#ifdef DEV_BUILD

class DevLogClass {
public:
    // mirrorSerial: also echo output to USB Serial TX (call site passes the
    // dev.txt toggle, e.g. devcfg.getBool("log_mirror_serial", true)).
    void begin(bool mirrorSerial);

    // Output — mirrors what main uses today. '\n' is expanded to CRLF for the
    // telnet client so raw telnet clients render lines correctly.
    void print  (const char* s);
    void println(const char* s);
    void printf (const char* fmt, ...);

    // Input — replaces Serial.available()/read()/readStringUntil(). Reads from
    // the telnet client only (serial RX is dead under USB MIDI).
    int    available();
    int    read();
    String readLine();   // up to '\n', short timeout, trimmed

private:
    void emit(const char* s, size_t len);   // to serial (raw) + client (CRLF)
    bool mirrorSerial_ = true;
};

extern DevLogClass DevLog;

#else  // !DEV_BUILD — stub: all debug I/O compiles to no-ops.

class DevLogClass {
public:
    void   begin(bool) {}
    void   print(const char*) {}
    void   println(const char*) {}
    void   printf(const char*, ...) {}
    int    available() { return 0; }
    int    read() { return -1; }
    String readLine() { return String(); }
};

inline DevLogClass DevLog;

#endif
