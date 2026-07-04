#include "DevConfig.h"

#ifdef DEV_BUILD

#include <LittleFS.h>

DevConfig devcfg;

void DevConfig::begin() {
    if (begun_) return;
    begun_ = true;
    count_ = 0;

    // LittleFS is already mounted by configInit() — do NOT re-mount. Missing
    // file simply yields an empty store.
    File f = LittleFS.open("/dev.txt", "r");
    if (!f) {
        Serial.println("[dev] dev.txt: not present (0 keys)");
        return;
    }

    while (f.available() && count_ < kMaxKeys) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;

        int eq = line.indexOf('=');
        if (eq < 0) continue;                 // no '=' → not a key/value line

        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        // Strip trailing inline comments? No — values may legitimately contain
        // '#'. dev.txt comments are whole-line only (documented). Keep value raw.
        key.trim();
        val.trim();
        if (key.length() == 0) continue;

        entries_[count_].key = key;
        entries_[count_].val = val;
        count_++;
    }
    f.close();

    Serial.printf("[dev] dev.txt: %d keys\n", count_);
}

const String* DevConfig::find(const char* key) const {
    for (int i = 0; i < count_; i++) {
        if (entries_[i].key.equals(key)) return &entries_[i].val;
    }
    return nullptr;
}

bool DevConfig::has(const char* key) const {
    return find(key) != nullptr;
}

String DevConfig::getStr(const char* key, const String& def) const {
    const String* v = find(key);
    return v ? *v : def;
}

int DevConfig::getInt(const char* key, int def) const {
    const String* v = find(key);
    if (!v || v->length() == 0) return def;
    // Reject non-numeric leading char (toInt() returns 0 on garbage — use def).
    char c = (*v)[0];
    if (!(c == '-' || c == '+' || (c >= '0' && c <= '9'))) return def;
    return v->toInt();
}

bool DevConfig::getBool(const char* key, bool def) const {
    const String* v = find(key);
    if (!v) return def;
    String s = *v;
    s.toLowerCase();
    if (s == "1" || s == "true" || s == "yes" || s == "on")  return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return def;
}

#endif  // DEV_BUILD
