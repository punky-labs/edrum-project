# `dev.txt` — dev-config file format

`dev.txt` is the **dev-plumbing** config file: WiFi creds and debug switches for the
head firmware's dev build. It is read once at boot from LittleFS by `DevConfig`
(`firmware/src/dev/DevConfig.{h,cpp}`).

- **Dev-build only.** The firmware only reads it when compiled with `-D DEV_BUILD`
  (currently `[env:xiao_esp32s3_head]`). A production build ignores it entirely.
- **Never shipped, never committed.** It holds WiFi creds. `data/dev.txt` is gitignored.
- **Separate from product config.** The binary `InputConfig` blob (`/config.bin`,
  managed by the app over SysEx) is a different file, untouched by this.

## Where it lives

On the device's LittleFS at `/dev.txt`. Andrew creates it locally and uploads it:

```
# put the file at <project>/data/dev.txt, then:
pio run -e xiao_esp32s3_head -t uploadfs
```

The firmware does **not** create or write `dev.txt` — it only reads it. If the file is
absent, boot continues normally and every key falls back to its compiled default (boot
prints `[dev] dev.txt: not present (0 keys)`).

## Format

- One `key=value` per line.
- Blank lines and lines starting with `#` are ignored.
- Split on the **first** `=`; whitespace around key and value is trimmed.
- Comments are **whole-line only** — a `#` after a value is part of the value.
  (The examples below put comments on their own lines for that reason.)
- Booleans: `1` / `true` / `yes` / `on` = true; `0` / `false` / `no` / `off` = false
  (case-insensitive). Anything else → the caller's compiled default.
- ~16 keys max. Unknown keys are stored harmlessly but unused; the parser knows no
  specific key names, so adding a future flag needs only a `getX()` call in the
  firmware, never a parser change.

## Keys currently consumed

| Key                 | Type   | Default | Meaning                                                                 |
|---------------------|--------|---------|-------------------------------------------------------------------------|
| `wifi_ssid`         | string | `""`    | WiFi network to join (STA). Empty → WiFi/telnet skipped.                |
| `wifi_pass`         | string | `""`    | WiFi password.                                                          |
| `debug_wifi`        | bool   | `false` | `1` = bring up WiFi + telnet debug console on TCP port 23.              |
| `log_mirror_serial` | bool   | `true`  | `1` = also echo `DevLog` output to USB Serial TX (handy during bring-up). |
| `diag_mode`         | bool   | `false` | `1` = boot into ADC-dump diagnostic mode (detection + MIDI off).        |

Notes:
- WiFi failure is a **silent skip**: missing creds, or a connect timeout (≤8 s), lets
  boot continue with telnet simply unavailable — it never stalls boot.
- When `debug_wifi=1` and the connect succeeds, the device IP is printed to USB Serial
  once at boot so you know the address to `telnet` to.
- `diag_mode` can still be toggled at runtime with the `m` command; `dev.txt` only sets
  its initial value.

## Example

Place this at `<project>/data/dev.txt`:

```
# eDrum dev config — place at /dev.txt on LittleFS (uploadfs). NOT shipped, NOT committed.
# key=value, one per line, # comments. All keys optional; firmware uses compiled
# defaults for anything absent.
wifi_ssid=BoalBench
wifi_pass=secretpass
# 1 = bring up WiFi + telnet debug console on port 23
debug_wifi=1
# 1 = also echo DevLog output to USB Serial TX
log_mirror_serial=1
# 1 = boot into ADC-dump diagnostic mode (detection off)
diag_mode=0
```

Then `telnet <device-ip>` gives a bidirectional dev console (the replacement for the
dead USB serial RX under USB MIDI): post-boot debug output streams to it, and typing
commands (`s`, `h`, `w 0 thresh 10`, `a`, `m`, …) drives the firmware.

## Reminder

`data/dev.txt` (and `firmware/data/dev.txt`) are in `.gitignore` so real creds are never
committed. Keep it that way.
