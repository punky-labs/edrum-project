# eDrum Dev Workflow — Tooling Roadmap

**Audience:** Andrew + Claude. This is the roadmap for *development tooling* — the
infrastructure that makes debugging and tuning faster. It is NOT product features and
NOT shipped to end users. All of it is dev-build-only.

**Companion:** `debugging_method.md` (the *process*); this doc is the *tools*.

**Motivation:** Two things hurt us in the Stage 2a session — (1) serial RX is dead
under USB MIDI so we lost interactive debugging on the head firmware, and (2) every
debug-switch or tuning change required an edit-compile-flash cycle (slow, and exposed
to the stale-cache trap). This tooling fixes both: a debug channel that doesn't clash
with MIDI, and a way to change switches/tuning WITHOUT recompiling.

---

## Design principles (the boundaries that keep this safe)

1. **Dev-only, compiled out of production.** Everything here lives behind a
   `DEV_BUILD` compile flag (or equivalent). A production build does not include the
   WiFi stack, telnet, or text-config reading, and behaves identically without them.
2. **Two separate config files, never merged:**
   - **Product config** — the binary `InputConfig` blob on LittleFS, managed by the
     app over SysEx. User-facing. The shipping mechanism. UNCHANGED by this tooling.
   - **Dev config** — text files, dev-build-only, never shipped. WiFi creds, debug
     switches, optional tuning overrides.
3. **The MCU stays dumb; the messy work lives on the computer.** Firmware does the one
   simple thing (read a text file at boot). Text↔SysEx round-tripping is a Python
   script on Andrew's machine, where string handling is cheap and safe.
4. **Simple text format:** `key=value`, one per line, `#` comments. Hand-rolled parser
   (~30 lines). NOT JSON/INI/YAML. Every key optional with a compiled default, so a
   missing/malformed file degrades gracefully.

---

## The two dev files

### `dev.txt` — dev plumbing (gitignored; holds secrets)
```
# eDrum dev config — NOT shipped, NOT committed
wifi_ssid=BoalBench
wifi_pass=...
debug_wifi=1          # enable WiFi telnet logging
diag_mode=0           # boot into ADC-dump diagnostic mode
use_text_tuning=0     # if 1, load tuning.txt and let it override saved config
log_level=2
```

### `tuning.txt` — tuning overrides (versionable; swappable)
Only read when `use_text_tuning=1` in dev.txt. Lets Andrew keep multiple named tuning
files on the computer (`tuning_kd80_soft.txt`, `tuning_pdx12_v2.txt`) and swap them.
```
# Per-input tuning overrides (real units). Every key optional.
input0_threshold=8
input0_scan_ms=3.0
input0_decay_len2_ms=350
...
```

---

## CRITICAL rule: text-tuning mode is read-only-from-text

When `use_text_tuning=1`, the device loads tuning from `tuning.txt` at boot and that is
the source of truth. In this mode, **do NOT persist changes back to the LittleFS binary
config** — otherwise you get a three-way disagreement between (a) text file, (b) saved
binary, (c) live RAM values. The mental model must stay simple:
- **text-tuning mode ON:** `tuning.txt` is truth. Persist by editing the text file.
- **text-tuning mode OFF (normal/production):** LittleFS binary + app is truth.

---

## Syncing values back from the firmware (two-way, but the two-way lives in Python)

The firmware ALREADY has full config export — the SysEx `PAD_GET` / `PRE_EXPORT`
commands the app uses. So "download the live values from the device" is already solved
in one direction. The clean design:
- **Firmware → text (download):** a Python dev script sends `PAD_GET`/`PRE_EXPORT`,
  receives live values over SysEx, writes them out as a `tuning.txt`. This snapshots
  whatever was tuned live (e.g. via the app) into a versionable file.
- **Text → firmware (upload):** either `uploadfs` the text file (firmware reads at
  boot), or the same script reads `tuning.txt` and sends SysEx set-commands live.

So the text file and SysEx become two front-ends to the same values, bridged by a
script. Tune live via the app (fast, interactive), then snapshot to text (versionable).
The MCU never parses/formats anything complex — Python does, where it's safe.

---

## Build order (each piece small and independently testable)

### Step 1 — dev.txt plumbing + flags
- Firmware (dev build only): read `dev.txt` from LittleFS at boot into a small struct.
  Key=value parser. Wire `diag_mode`, `log_level`, etc. to existing flags so they're
  set from the file instead of hardcoded.
- **Success:** change a debug flag by editing dev.txt + uploadfs + reset — no recompile.

### Step 2 — WiFi telnet/TCP serial (dev build only)
- Firmware connects to WiFi using creds from dev.txt, opens a TCP server; a telnet
  client receives what `Serial.printf` would have printed (and can send commands back).
- This is the replacement for the dead USB serial RX — bidirectional dev console that
  doesn't touch USB or clash with MIDI.
- **Success:** `telnet <device-ip>` gives a working in/out console while USB MIDI runs.
- **Note:** S3 is BLE-only (no classic BT / SPP) — WiFi TCP is the route, not BT serial.

### Step 3 — firmware reads tuning.txt at boot (under use_text_tuning flag)
- When the flag is set, parse `tuning.txt` and apply as overrides after loading config.
- Honour the read-only-from-text rule (don't persist back to LittleFS in this mode).
- **Success:** swap tuning by uploading a different tuning.txt — no recompile, no app.

### Step 4 — Python round-trip script
- `download`: PAD_GET/PRE_EXPORT → write tuning.txt.
- `upload`: read tuning.txt → SysEx set-commands (live) OR drive uploadfs.
- **Success:** tune live in the app, snapshot to a named text file, reload later.

---

## Hardware-independent development (bare-module / no-pad dev)

**Motivation:** Andrew develops from two locations — the home studio (full kit) and the
office desk (no pads). Carrying the test module back and forth is a pain, and spare
XIAO ESP32-S3 modules are tiny and plentiful. Most of the stack does NOT need real
pads: the entire SysEx control plane, app↔firmware integration, app UI, build/boot/
LittleFS, the dev-config work, WiFi telnet, and even the DSP *logic* can all be
developed and tested without a pad. What genuinely needs pads is narrow: final
feel-tuning, real noise-floor/crosstalk validation. This is the firmware-side analogue
of the app's emulator — decouple development from physical pads wherever possible.

Two tiers, increasing power:

### Tier 1 — Bare-module dev mode (simple)
A dev flag so the firmware tolerates nothing being plugged in, and optionally injects
synthetic activity to develop against:
- **All inputs disabled by default** (no floating-jack phantom hits — the exact problem
  diagnosed in the Stage 2a session). `InputConfig.enabled` already supports this.
- **Synthetic hit injection:** optionally fire fake `05 03` hit events on a timer (e.g.
  one per second, cycling inputs/zones/velocities) so the app has data to render. This
  is what lets "app hit log not displaying" be developed/fixed on a bare module with no
  pad. Same idea as the app emulator, but originating in firmware.
- **Success:** plug a bare XIAO into the office desktop, run the app, develop the full
  control plane + app integration with realistic-looking traffic, no pads.

### Tier 2 — Recorded-waveform playback through the real engine (powerful)
The holy grail for solo DSP work: feed *recorded real pad samples* through the actual
`processBlock` instead of live ADC.
- Capture a **library of real hits** as raw sample arrays (head/rim/choke, soft/hard,
  the hard-hit-runaway case) — stored as files (same file-based philosophy as
  `tuning.txt`). The Stage 2a ADC captures (~900 PDX-12 hit, noise floor) are the first
  seeds of this.
- A dev mode that replays a captured waveform through the engine, so at the office desk
  (bare module, or even host-only) Andrew can:
  - Develop + **regression-test the detection algorithm against real signals**,
  - Verify a tuning change does what's expected against a known hit,
  - **Reproduce the hard-hit-runaway deterministically** from a captured waveform
    instead of needing to wail on a mesh pad (directly serves debugging_method's
    "reproduce deterministically"),
  - Build a proper test suite ("these 20 captured hits → these 20 expected velocities").
- This turns "tune by feel against a pad" into "tune against a reproducible dataset" —
  faster, testable, location-independent. Final feel-tuning still needs the real kit,
  but the bulk of algorithm work no longer does.

**Synergy:** recorded waveforms are just files, like `tuning.txt`. The "office dev"
setup becomes: bare XIAO (or no hardware) + a dev flag + a folder of captured waveforms
+ tuning files. Everything except final feel-tuning becomes reproducible at the desk.
Sequencing-wise, Tier 1 is small and pairs with the dev-config work; Tier 2 is a larger
investment best taken when algorithm tuning/regression becomes the main activity
(i.e. mid-Stage-2).

---

## Coexistence note (forward-looking, not Step-1 concern)

WiFi (for telnet, and later ESP-NOW) and BLE (for future BLE MIDI) share the S3's one
2.4 GHz radio, time-sliced. ESP-IDF supports WiFi+BLE coexistence, but it adds latency
jitter. ESP-NOW latency is the thing we're protecting (the reason satellites use it).
So: dev telnet over WiFi is fine now (no ESP-NOW running yet on the head dev unit);
but running BLE MIDI + ESP-NOW simultaneously on shipping hardware is a thing to
MEASURE, not assume. Out of scope for this tooling; flagged for the wireless phase.

---

## Relationship to the product

None of this is product. The product path is: app ↔ USB MIDI SysEx ↔ head unit ↔
ESP-NOW ↔ satellites. This tooling is scaffolding that disappears in production builds.
The one piece of lasting product value that may emerge: the SysEx config export already
exists, and the Python round-trip script exercises it — which doubles as a test of the
real config-sync path the app uses.
