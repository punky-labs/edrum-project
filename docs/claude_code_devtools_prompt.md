# Claude Code Prompt — Dev Tooling: dev.txt config + WiFi telnet debug console

> Paste this whole file as the task. Implements Steps 1+2 of
> `docs/dev_workflow_plan.md`: a generic dev-config file reader (`dev.txt`) and a
> WiFi telnet debug console that REPLACES the dead USB-serial-RX path on the head
> firmware. All of it is DEV-ONLY (compiled behind `DEV_BUILD`).

## Context — read these first (via MCP filesystem)
- `docs/dev_workflow_plan.md` — the roadmap (esp. "Design principles", "The two dev
  files", "Build order" Steps 1 & 2). THIS is the spec.
- `docs/debugging_method.md` — how we work (observe before theorising, one change at a
  time). Relevant because the acceptance criteria are staged for exactly that.
- `docs/project_state.md` — "START HERE" section: current hardware/debugging constraints.
  KEY FACT: serial RX is dead under USB MIDI (`ARDUINO_USB_MODE=0`); this telnet console
  is the replacement. Serial TX still works and is KEPT for early-boot lines only.
- `firmware/src/main_esp32s3.cpp` — the head firmware being migrated.
- `firmware/src/config/Config.cpp` — shows the LittleFS usage pattern to mirror.
- `platformio.ini` — `[env:xiao_esp32s3_head]` is the target env.

## Target
Head firmware ONLY: `[env:xiao_esp32s3_head]` (XIAO ESP32-S3, COM13, `ARDUINO_USB_MODE=0`
+ TinyUSB USB MIDI). Do NOT touch `adc_diag` / bench / test envs. **Do NOT touch the
USB / TinyUSB / MIDI setup** — WiFi/telnet is independent of USB and must not disturb it.

---

## Decisions already made (do NOT redesign these)

1. **DIY `WiFiServer` on port 23 — NO external telnet library.** We wrap it in our own
   `DevLog` abstraction, so a library's `Stream` interface adds a dependency without
   value. Raw TCP on port 23 works with all telnet clients for line-based logging.
2. **`DevLog` abstraction is the single seam.** All firmware debug output and command
   input goes through `DevLog`. The telnet `WiFiServer` is its sink. The abstraction
   exists so the sink can be swapped later without touching call sites.
3. **`DevConfig` is a GENERIC string key→value store**, NOT a fixed struct. The parser
   reads WHATEVER keys are in `dev.txt` into a generic map. Consumers ask by name with a
   compiled default: `devcfg.getStr("wifi_ssid", "")`, `devcfg.getInt("log_level", 2)`,
   `devcfg.getBool("debug_wifi", false)`. **Adding a future flag must NOT require editing
   the parser** — only add a `getX()` call at the consuming site. Values stored as
   strings; typed getters convert on read. Call site owns the default; missing/malformed
   key → the caller's default.
4. **WiFi failure = silent skip.** Missing creds, or connect fails/times out → boot
   continues normally, telnet simply unavailable. No AP fallback (deferred). Non-blocking:
   a missing network must never stall boot.
5. **Early-boot lines stay on USB `Serial`** (TX works pre-WiFi, doesn't disturb MIDI
   which isn't active yet). Everything AFTER WiFi bring-up goes through `DevLog` (which
   writes to telnet, and MAY also mirror to `Serial` TX — see below). Trim the boot
   `delay(5000)` to `delay(2000)`.
6. **Everything DEV-ONLY**, behind a `DEV_BUILD` compile flag. In a non-DEV build,
   `DevLog`/`DevConfig`/`DevWiFi` compile to no-ops (or are excluded) and the firmware
   behaves exactly as now. Add `-D DEV_BUILD` to `[env:xiao_esp32s3_head]` for now.

---

## New files

### `firmware/src/dev/DevConfig.{h,cpp}` — generic dev-config store
- Reads `/dev.txt` from LittleFS at boot (LittleFS is already mounted by `configInit()`
  — do NOT re-mount; just `LittleFS.open("/dev.txt", "r")`).
- Parse: line-based. Ignore blank lines and lines starting with `#`. Split each line on
  the FIRST `=`. Trim whitespace on key and value. Store in a generic container
  (e.g. a small fixed-size array of key/value String pairs, or std::map<String,String>
  — keep it simple, ~16 keys max is plenty).
- API (typed getters, call-site default):
  ```cpp
  void   begin();                                   // read+parse /dev.txt (once)
  bool   has(const char* key) const;
  String getStr (const char* key, const String& def) const;
  int    getInt (const char* key, int def) const;
  bool   getBool(const char* key, bool def) const;  // "1"/"true"/"yes" = true
  ```
- **Parser knows NO specific key names.** Generic. If `/dev.txt` is missing, `begin()`
  succeeds with an empty store (every getter returns its caller default).

### `firmware/src/dev/DevLog.{h,cpp}` — the output/input abstraction (the seam)
- A small module (namespace or singleton) that the rest of the firmware calls instead of
  `Serial` for DEBUG I/O. NOT for MIDI. NOT for the early-boot lines.
- Output API mirroring what main uses today:
  ```cpp
  void print  (const char* s);
  void println(const char* s);
  void printf (const char* fmt, ...);   // vsnprintf into a buffer, then emit
  ```
- Input API replacing `Serial.available()/read()/readStringUntil()`:
  ```cpp
  int  available();
  int  read();
  String readLine();     // read up to '\n', with a short timeout, trimmed
  ```
- Internally routes to the active sink = the telnet `WiFiServer` client (via DevWiFi).
  Optionally ALSO mirror output to `Serial` TX (harmless, occasionally useful) — make
  this a compile-time or DevConfig toggle (`getBool("log_mirror_serial", true)`).
- If no telnet client is connected, output is simply dropped (do not block). Input
  returns "nothing available".
- Behind `DEV_BUILD`: if not defined, all functions are empty inlines.

### `firmware/src/dev/DevWiFi.{h,cpp}` — WiFi bring-up + telnet server
- `begin(DevConfig&)`:
  - If `getBool("debug_wifi", false)` is false → do nothing (return).
  - Else read `wifi_ssid`/`wifi_pass`; if ssid empty → skip silently.
  - `WiFi.mode(WIFI_STA); WiFi.begin(ssid, pass);` with a NON-BLOCKING wait: poll for
    connection up to a timeout (e.g. 8s) but do it so boot proceeds — simplest is to
    attempt connect, and if not connected within the timeout, log "WiFi: no connection,
    telnet disabled" to `Serial` and carry on. (A fully async approach is nicer but a
    bounded blocking wait ≤8s is acceptable for a dev build — pick the simpler one.)
  - On connect: start `WiFiServer` on port 23, print the device IP to `Serial` (so you
    can see the address to telnet to over USB at least once), and mark telnet available.
- `poll()`: called every `loop()`. Accept a new client if one is waiting (single client;
  if a new one connects, replace the old). Maintain the connection. Expose the current
  client to `DevLog` for read/write.
- All behind `DEV_BUILD`.

---

## Migration in `main_esp32s3.cpp`

Replace DEBUG serial I/O with `DevLog`. Be surgical — MIDI and early boot stay on Serial.

**KEEP on `Serial` (do not change):**
- The pre-WiFi boot lines in `setup()`: `[eDrum] Ready.`, the build stamp, `[LED] boot`,
  the LittleFS/config lines, the `[ADC] configured ...` line, `[LED] ready`. These print
  before/around WiFi bring-up and are the "attach USB serial to see boot" safety net.
- Trim `delay(5000)` → `delay(2000)`.

**MOVE to `DevLog` (post-boot debug I/O):**
- `printHelp()` output.
- All of `handleSerial()`'s output (`[>]`, `[Config]`, `[SCOPE]`, `[ADC] Dump`, `[DBG]`,
  `[DIAG]`, `[w]`, usage lines).
- The command INPUT: the `loop()` `Serial.available()/read()` → `DevLog.available()/read()`;
  and inside `handleSerial`, the `Serial.readStringUntil('\n')` for `o`/`w` →
  `DevLog.readLine()`.
- The `loop()` debug prints: `[ADC] measured ...`, `[LED] mounted/unmounted`, the `[ADC]`
  dump block, `[WARN] overrun`, `[HIT]`/`[RIM]` (already gated by g_hitDebug), `[CHOKE]`,
  the scope `[SCOPE]` dumps.
- NOTE the scope `[SCOPE]` output currently targets the desktop scope tool over serial;
  that tool is PARKED (project_state.md). Route its prints through DevLog too (over
  telnet) — do not special-case it.

**Wire up in `setup()` (after `configInit()/configLoad()`, since LittleFS must be mounted):**
```cpp
devcfg.begin();                       // read /dev.txt
// diag_mode now file-controlled (was hardcoded default false):
g_diagMode = devcfg.getBool("diag_mode", false);
devwifi.begin(devcfg);                // connect + start telnet (silent skip on fail)
```
**Wire up in `loop()`:** call `devwifi.poll();` once per iteration (near the top, by
`stream.pump()` is fine).

**`g_diagMode`:** keep the variable and the `m` command, but its INITIAL value now comes
from `devcfg.getBool("diag_mode", false)` instead of the hardcoded `= false`. (Runtime
`m` toggle still works.)

---

## platformio.ini (`[env:xiao_esp32s3_head]` only)
- Add `-D DEV_BUILD` to `build_flags`.
- Add the new sources to `build_src_filter`:
  `+<dev/DevConfig.cpp>`, `+<dev/DevLog.cpp>`, `+<dev/DevWiFi.cpp>`.
- No new `lib_deps` (WiFi + WiFiServer are in the ESP32 Arduino core; LittleFS already used).
- Do NOT modify other envs.

---

## Do NOT do
- No external telnet library.
- No changes to USB / TinyUSB / MIDI setup, or to the SysEx protocol.
- No changes to Layers 1/2 (AdcSampler/SampleStream) or the detection engine.
- No AP fallback, no OTA, no web server — just WiFi STA + TCP port 23.
- No changes to `Config.{h,cpp}` product-config format (dev.txt is a SEPARATE file).
- Do not make the DevConfig parser aware of specific key names.

## Acceptance criteria (staged — test in this order)
1. `[env:xiao_esp32s3_head]` builds clean with `-D DEV_BUILD`. **Full clean build**
   (`pio run -e xiao_esp32s3_head -t clean` first — stale-cache guard).
2. **Stage A (parse):** with a `/dev.txt` present, boot prints (over USB Serial) the
   parsed keys or at least confirms dev.txt was read — add a boot line like
   `[dev] dev.txt: N keys` so we can confirm parsing without WiFi.
3. **Stage B (WiFi):** with valid `wifi_ssid`/`wifi_pass` + `debug_wifi=1`, boot connects
   and prints the device IP to USB Serial. With bad/missing creds, boot continues and
   prints a "telnet disabled" line — NO hang.
4. **Stage C (telnet out):** `telnet <device-ip>` shows post-boot debug output (e.g. hit
   `h` locally... no — see next). Trigger a pad or send a test note; the `[HIT]`/status
   lines appear in the telnet session.
5. **Stage D (telnet in):** typing `s`, `h`, `w 0 thresh 10`, `a`, `m` into the telnet
   session drives the firmware exactly as USB serial used to. This is the key win —
   bidirectional dev console under USB MIDI.
6. USB MIDI still works throughout (MidiView shows notes) — telnet must not disturb it.

## Report back
- Build status (confirm clean build done).
- Whether the WiFi connect was blocking or async, and the timeout used.
- Any ESP32 Arduino-core WiFi/WiFiServer quirks encountered.
- Confirm the DevConfig parser contains NO hardcoded key names (generic store).
- Note RAM impact of adding the WiFi stack (it's significant — report the delta).

---

## Also produce: documented `dev.txt` format
Write a template/example to `docs/dev_txt_format.md` (NOT to /data — Andrew will create
the actual `dev.txt` and uploadfs it himself). Document every key currently consumed,
its type, default, and meaning. Example content:
```
# eDrum dev config — place at /dev.txt on LittleFS (uploadfs). NOT shipped, NOT committed.
# key=value, one per line, # comments. All keys optional; firmware uses compiled
# defaults for anything absent.
wifi_ssid=BoalBench
wifi_pass=secretpass
debug_wifi=1            # 1 = bring up WiFi + telnet debug console on port 23
log_mirror_serial=1     # 1 = also echo DevLog output to USB Serial TX
diag_mode=0             # 1 = boot into ADC-dump diagnostic mode (detection off)
```
Note in that doc: add `data/dev.txt` (or wherever the fs source dir is) to `.gitignore`
so real creds are never committed.
