// DevConfig — generic dev-config store (dev-build only).
//
// Reads /dev.txt from LittleFS at boot into a generic key->value string map.
// The parser knows NO specific key names: it stores WHATEVER keys are present.
// Consumers ask by name with a compiled default (the call site owns the default;
// a missing/malformed key yields the caller's default). Adding a future flag
// requires only a getX() call at the consuming site — never a parser edit.
//
// Product config (the binary InputConfig blob) is a SEPARATE file, unchanged by
// this. See docs/dev_txt_format.md for the documented key set.
#pragma once
#include <Arduino.h>

#ifdef DEV_BUILD

class DevConfig {
public:
    // Read + parse /dev.txt once. Missing file => empty store (all getters
    // return their caller default). Never fails hard; never re-mounts LittleFS.
    void   begin();

    bool   has   (const char* key) const;
    String getStr (const char* key, const String& def) const;
    int    getInt (const char* key, int def) const;
    bool   getBool(const char* key, bool def) const;   // "1"/"true"/"yes"/"on" = true

    int    count() const { return count_; }

private:
    static const int kMaxKeys = 16;
    struct Entry { String key; String val; };
    Entry entries_[kMaxKeys];
    int   count_ = 0;
    bool  begun_ = false;

    const String* find(const char* key) const;
};

extern DevConfig devcfg;

#else  // !DEV_BUILD — stub: every getter returns the caller default.

class DevConfig {
public:
    void   begin() {}
    bool   has(const char*) const { return false; }
    String getStr(const char*, const String& def) const { return def; }
    int    getInt(const char*, int def) const { return def; }
    bool   getBool(const char*, bool def) const { return def; }
    int    count() const { return 0; }
};

inline DevConfig devcfg;

#endif
