// DevWiFi — WiFi STA bring-up + a raw TCP (telnet) server on port 23 (dev-build
// only). This is the replacement for the dead USB-serial-RX path: under USB MIDI
// (ARDUINO_USB_MODE=0) serial RX wedges, so the interactive dev console lives on
// telnet instead. WiFi/telnet is INDEPENDENT of USB — it must not disturb MIDI.
//
// No external telnet library: a bare WiFiServer on port 23 works with every
// telnet client for line-based logging. DevLog is the seam that routes debug I/O
// to the current client (see DevLog.h).
//
// Failure is a silent skip: missing creds / connect timeout => boot continues,
// telnet simply unavailable. Never stalls boot beyond the bounded connect wait.
#pragma once
#include <Arduino.h>

class DevConfig;

#ifdef DEV_BUILD

// arduino-esp32 3.x makes WiFiClient/WiFiServer type aliases (NetworkClient/
// NetworkServer), so they can't be forward-declared — include the header.
#include <WiFi.h>

class DevWiFi {
public:
    // Bring up WiFi + telnet from dev.txt. If debug_wifi is false, or ssid is
    // empty, or the connect times out => returns having done nothing harmful.
    // Bounded blocking connect wait (see kConnectTimeoutMs); dev-build only.
    void begin(DevConfig& cfg);

    // Call once per loop(): accept/replace the single telnet client, maintain it.
    void poll();

    // Sink accessors used by DevLog. Safe to call when telnet is unavailable.
    bool   clientConnected();
    size_t clientWrite(const uint8_t* buf, size_t len);
    int    clientAvailable();
    int    clientRead();

private:
    static const uint32_t kConnectTimeoutMs = 8000;
    static const uint16_t kTelnetPort       = 23;

    WiFiServer server_{kTelnetPort};   // ctor just stores the port; begin() starts it
    WiFiClient client_;                // default = not connected
    bool       available_ = false;     // WiFi connected + server started
};

extern DevWiFi devwifi;

#else  // !DEV_BUILD — stub: no WiFi, no telnet.

class DevWiFi {
public:
    void   begin(DevConfig&) {}
    void   poll() {}
    bool   clientConnected() { return false; }
    size_t clientWrite(const uint8_t*, size_t) { return 0; }
    int    clientAvailable() { return 0; }
    int    clientRead() { return -1; }
};

inline DevWiFi devwifi;

#endif
