#include "DevWiFi.h"

#ifdef DEV_BUILD

#include "DevConfig.h"
#include <ArduinoOTA.h>

DevWiFi devwifi;

// Capture the most recent WiFi disconnect reason code so a failed association can
// report WHY (auth fail / AP not found / handshake timeout / ...). The event fires
// asynchronously during WiFi.begin()'s attempts.
static volatile uint8_t s_lastDisconnectReason = 0;

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        s_lastDisconnectReason = info.wifi_sta_disconnected.reason;
    }
}

// Human-readable-ish label for a WiFi.status() code (WL_*).
static const char* statusName(int s) {
    switch (s) {
        case WL_IDLE_STATUS:     return "IDLE";
        case WL_NO_SSID_AVAIL:   return "NO_SSID_AVAIL (AP not found / SSID mismatch)";
        case WL_SCAN_COMPLETED:  return "SCAN_COMPLETED";
        case WL_CONNECTED:       return "CONNECTED";
        case WL_CONNECT_FAILED:  return "CONNECT_FAILED (auth/handshake)";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED:    return "DISCONNECTED";
        default:                 return "UNKNOWN";
    }
}

void DevWiFi::begin(DevConfig& cfg) {
    if (!cfg.getBool("debug_wifi", false)) {
        // WiFi/telnet not requested — leave the radio alone.
        return;
    }

    String ssid = cfg.getStr("wifi_ssid", "");
    String pass = cfg.getStr("wifi_pass", "");
    if (ssid.length() == 0) {
        Serial.println("[dev] WiFi: debug_wifi set but wifi_ssid empty — telnet disabled");
        return;
    }

    // Bounded blocking connect. A fully async approach is nicer but a ≤8s wait
    // is acceptable for a dev build, and much simpler. Boot proceeds either way.
    Serial.printf("[dev] WiFi: connecting to \"%s\" (pass %u chars) ...\n",
                  ssid.c_str(), (unsigned)pass.length());
    WiFi.onEvent(onWiFiEvent);
    WiFi.persistent(false);       // don't wear flash storing creds each boot
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);         // no modem sleep during dev (also steadier assoc)
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < kConnectTimeoutMs) {
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
        int st = WiFi.status();
        Serial.printf("[dev] WiFi: no connection (status=%d %s, last_disconnect_reason=%u) "
                      "telnet disabled\n",
                      st, statusName(st), (unsigned)s_lastDisconnectReason);
        // Common reason codes: 2=AUTH_EXPIRE, 15=4WAY_HANDSHAKE_TIMEOUT (bad pass),
        // 201=NO_AP_FOUND, 205=CONNECTION_FAIL, 8=ASSOC_LEAVE.
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return;
    }

    server_.begin();
    server_.setNoDelay(true);
    available_ = true;

    // OTA (filesystem uploads only, in practice — see docs/dev_workflow_plan.md).
    // No password: dev-build-only, bench-network use. ArduinoOTA technically also
    // accepts a sketch (firmware) push (`pio run -t upload` vs `-t uploadfs`), but
    // the workflow this exists for is fast dev.txt/LittleFS tweaks via `uploadfs`
    // over WiFi instead of the USB unplug/bootloader-button dance.
    ArduinoOTA.setHostname("edrum-head");
    ArduinoOTA.onStart([]() {
        const char* type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.printf("[dev] OTA start (%s)\n", type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("[dev] OTA end — rebooting");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint32_t s_lastPct = 255;
        uint32_t pct = (total > 0) ? (progress * 100u / total) : 0;
        if (pct != s_lastPct) {
            s_lastPct = pct;
            Serial.printf("[dev] OTA progress: %u%%\n", (unsigned)pct);
        }
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[dev] OTA error [%u]\n", (unsigned)error);
    });
    ArduinoOTA.begin();

    Serial.printf("[dev] WiFi connected: %s  telnet port %u, OTA (uploadfs) ready\n",
                  WiFi.localIP().toString().c_str(), (unsigned)kTelnetPort);
}

void DevWiFi::poll() {
    if (!available_) return;

    ArduinoOTA.handle();

    // Accept a NEW connection if one is waiting; a new client replaces the old
    // (single-client console). accept() returns a connected client only on a new
    // connection, so this does not disturb an established session.
    WiFiClient nc = server_.accept();
    if (nc) {
        if (client_.connected()) client_.stop();
        client_ = nc;
        client_.setNoDelay(true);
        client_.print("[dev] eDrum telnet console\r\n");
    }

    // Reap a dropped client so clientConnected() reflects reality.
    if (client_ && !client_.connected()) {
        client_.stop();
    }
}

bool DevWiFi::clientConnected() {
    return client_.connected();
}

size_t DevWiFi::clientWrite(const uint8_t* buf, size_t len) {
    if (!client_.connected()) return 0;
    return client_.write(buf, len);
}

int DevWiFi::clientAvailable() {
    if (!client_.connected()) return 0;
    return client_.available();
}

int DevWiFi::clientRead() {
    if (!client_.connected()) return -1;
    return client_.read();
}

#endif  // DEV_BUILD
