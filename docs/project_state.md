# eDrum Project State
Last updated: 2026-07-12 (Scan Redesign v3 + EMA + tunable constants
hardware-validated — working trigger module across 3 pad types, no audible
double-triggering or missed hits. See "Scan Redesign" section.)

---

## ⮕ START HERE — Working method & debugging (read first)

The dev team is just Andrew + Claude. We adopted a methodical, anti-rabbit-hole
process after a Stage 2a session burned hours/tokens on code-reading and guessing.
**At the start of any eDrum debugging/dev task, read these:**
- **`docs/debugging_method.md`** — the systematic process to follow when something
  breaks (observe before theorising, instrument before changing, one variable at a
  time, confirm fresh binary). Run this loop; don't freelance.
- **`docs/dev_workflow_plan.md`** — the dev-tooling roadmap (WiFi telnet console, dev
  config file, tuning file, Python sync script). The order of operations for tooling
  we build next.

### Hardware / debugging constraints (HARD-WON — current truth)
- **Serial RX is DEAD under USB MIDI (`ARDUINO_USB_MODE=0`).** Serial TX works (boot
  banner, ADC dump appear) but host→device input wedges. **Cannot drive the head
  firmware interactively over serial.** Drive it via the app (SysEx over MIDI) or via
  compile-time defaults + reflash. Interactive serial commands only work on the
  `adc_diag` firmware.
- **`adc_diag` firmware (`[env:xiao_adc_diag]`, MODE=1, native USB-CDC, no MIDI)** gives
  clean bidirectional serial for raw-signal observation. Use it for pure-sensing work
  where MIDI isn't needed. (`firmware/src/adc_diag.cpp`.)
- **Stale pioarduino build cache silently flashes OLD binaries.** If behaviour is
  unchanged after an edit, or weird/repeating: **full clean build**
  (`pio run -e <env> -t clean`) before assuming the change was wrong. This cost hours.
- **Boot build stamp** (`[eDrum] Build stamp: <date> <time>`) confirms the flashed
  binary is fresh. Check it after every flash.
- **Floating (unplugged) jacks read 12–17 noise** (high-impedance antenna pickup);
  **populated jacks read ~4.** Empty jacks generate phantom hits. Disable unused inputs
  (`InputConfig.enabled` / serial `w <i> enable 0`) or plug them when testing.
- **Head/rim GPIO assignment was backwards from board bring-up until 2026-07-06.**
  `kHeadCh`/`kRimCh` in `main_esp32s3.cpp` had tip/ring reversed relative to the
  actual PCB — confirmed via single-piezo pads (KD-80, which cannot physically
  produce a rim signal) on jacks 0, 1, and 3: real signal consistently landed on
  the GPIO labelled "rim", not "head". The ESP32-S3 GPIO-to-ADC1-channel formula
  itself (`channel = gpio - 1`) is confirmed correct against the datasheet — this
  was a labelling swap in firmware, not an ADC bug, not per-jack wiring, and not
  crosstalk (all three were considered and ruled out with real pad tests before
  landing on this). If any single-piezo-pad test ever again shows strong signal
  on the "rim" channel, check this assignment first before assuming a new bug.
- **PDrumTrigger spike-rejection self-lock (found + fixed 2026-07-06).** The
  HelloDrum-derived spike-rejection filter in `PDrumTrigger::sensing()`
  (`firmware/src/sensing/pdrum/PDrumTrigger.cpp`) stored the REJECTED
  (substituted) value into its own history (`prevPiezoValue`/`prevPrevPiezoValue`)
  instead of the true incoming sample. Once two consecutive real samples exceeded
  `SPIKE_THRESHOLD` (200) from a frozen reference — trivial for a fast piezo
  attack peaking in the thousands — every subsequent real sample kept comparing
  against the same stale pair and got rejected again, permanently, producing a
  "machine-gun" flood of identical max-velocity hits that only released when a
  sample's delta happened to fall back under threshold by chance. Confirmed with
  data, not guesswork: added a `spikeRejectCount_` counter (TEMP DIAGNOSTIC
  pattern) and watched it climb by ~1/sample continuously for the full duration
  of a live runaway (thousands of rejections in ~2 seconds), while a simultaneous
  `a` ADC dump on the same channel showed completely normal noise-floor values —
  cleanly proving the fault was in `PDrumTrigger`'s consumption of the data, not
  in sampling/hardware. Fix: history now always tracks the true incoming sample
  (captured before any substitution), applied identically to both head and rim
  channels. If a similarly-shaped "reject an outlier, substitute a previous
  value" filter is ever written elsewhere in this codebase, check that its
  history/state update uses the true input, not the substituted output — this
  exact self-poisoning pattern is easy to reintroduce.
  Also worth noting: this reproduced most reliably via a rapid stick-bounce on
  the pad (many large transients in quick succession), and had never been seen
  on the old RP2040 build running the same core algorithm — consistent with a
  fixed `SPIKE_THRESHOLD` tuned for different ADC range/sample-rate hardware.
- **Retrigger-cancel v1 (added 2026-07-06) is BROKEN and SUPERSEDED — do not
  patch it, it has been replaced by a full redesign.** See the "Retrigger-Cancel
  Design Spec (v2, 2026-07-07)" section below for the agreed replacement
  mechanism; this entry is kept only as the historical record of what was tried
  and why it failed. Added a samples-to-peak (`peakSampleIdx`) based accept/reject
  check to `PDrumTrigger::sensing()` (repurposing the previously-dead `retrig`
  config field as the cutoff), on the theory that a genuine strike's attack is
  near-instant (~1-2 samples, per earlier scope captures) while a mesh head's
  own mechanical rebound rises more slowly (~7-14 samples, also per earlier scope
  captures). It successfully eliminated the machine-gun retrigger cascade. BUT
  live single-hit testing with full per-event visibility (`[REJECT]`/`peakidx=`
  debug fields) showed the OPPOSITE of intended: genuine hard strikes
  (`velraw` 4088/1076/1056, unmistakably real) were REJECTED with `peakidx` in
  the high-20s/low-30s, while small residual tail events (`truepeak` 34-870)
  were ACCEPTED with `peakidx` 0-5. This contradicts the earlier scope-based
  characterisation (fast 2-3 sample rise to plateau) for the same pad/scenario.
  **Root design flaw identified 2026-07-07:** v1 measured "samples until THIS
  window's own peak" in isolation — it had no concept of comparing against the
  previous peak's level/trend, which is what actually distinguishes a decaying
  oscillation from a genuine new strike. A single-window attack-rate proxy
  cannot substitute for a real peak-to-peak trend comparison. v2 (see below)
  fixes this structurally rather than by re-tuning v1's cutoff number.
  **Two side-bugs found during v1 investigation, still parked/unconfirmed, kept
  for reference in case they recur or turn out relevant to v2:**
  1. Scope-capture duplication bug: two `[SCOPE]` dumps ~617ms apart contained
     bit-identical `T,H,R` sample data, despite `SampleStream`'s ring depth (8192
     @ 8kHz, ~1.02s) being nowhere near a wraparound for that gap. `PDrumTrigger`'s
     `crossAbsIndex_`/`triggerBack_` logic and `SampleStream::readWindow()` both
     look structurally correct on inspection — the bug is likely in how they
     interact at runtime (e.g. `g_scopeSnap` computation in `main_esp32s3.cpp`'s
     arm-time logic), not visible from static code reading alone.
  2. The `peakSampleIdx` values themselves clustering suspiciously close to a
     full `scantime`-worth of samples (~24 @ 8kHz for 3ms) for genuine hits —
     this may or may not be connected to (1); not required to resolve either to
     implement v2, since v2 doesn't rely on this specific measurement.
  All TEMP DIAGNOSTIC instrumentation from this investigation is left in place
  (`[REJECT]` print, `peakidx=` on `[HIT]`/`[RIM]`, `hasReject()`/`clearReject()`
  latch pattern) — some of it (the reject-event visibility pattern) is directly
  reusable for v2's implementation.
- Future debug channel: **WiFi telnet/TCP console** (dev-build-only) replaces the dead
  USB serial RX without clashing with MIDI. See `dev_workflow_plan.md`.

### Stage 2a status (2026-06-30)
- **The real Edrumulus detection core is ported and WORKS on hardware.** Clean velocity
  scaling, good SNR (hit ~900 vs noise ~4–16). The long phantom-hit saga was NOT an
  algorithm bug — it was floating jacks + stale cache.
- DC-offset IIR added before band-pass + spike-cancel (the unipolar front-end needs it;
  Edrumulus does this in `process()` before `process_sample`).
- `InputConfig.enabled` per-channel enable added; boot build stamp added; `g_diagMode`
  + `adc_diag.cpp` standalone diagnostic firmware added.
- **Open (app-side, next): app hit log not displaying; settings persistence over SysEx.**
  Firmware send/receive paths verified correct; issue is app-side or app↔device. Tackle
  with the debugging_method (check app log `RX:`/`TX:`, confirm direction first).

---

## Sensing Rewrite — Status (2026-06-29)

Replacing the simple peak-picker with an Edrumulus-derived power-domain engine.
Full design in `docs/sensing_rewrite_step0.md` (params/decisions) and
`docs/sensing_rewrite_step1_plan.md` (architecture/staging).

**Stage 1 — COMPLETE (sensing pipeline proven).**
The new 3-layer architecture is built and validated on hardware:
- Layer 1 `AdcSampler` — owns ADC1 + `adc_continuous` DMA. Confirmed delivering
  **8000 Hz/ch** on the XIAO ESP32-S3 head unit (8 ch, 64 kHz aggregate).
- Layer 2 `SampleStream` — ring buffer (8 ch × 8192 ≈ 1.0 s), gapless cursor reads
  with overrun detection, `readWindow()` for capture.
- Layer 3 `PDrum2Trigger` — SINGLE_PIEZO **simple time-domain** detector (placeholder;
  the real band-pass/decay engine is Stage 2). Sample-count timing, no millis().
- `TriggerEngine` interface changed: `sensing()` → `initialize()` + `processBlock()`.
- `main_esp32s3.cpp` rewritten to pump→read→processBlock; old `analogRead` loop and
  global `ring_buffer.h` removed. PDrum v1 retired (no legacy to service).
- KD-80 on jack 2 triggers cleanly via the new pipeline; velocities scale soft→hard.

**Bugs fixed during Stage 1 review:**
- `AdcSampler` ADC channel pattern masked `ch & 0x7` — collides ADC1 ch8/ch9
  (GPIO9/10) with ch0/1. Would corrupt jack 3. Now assigns channel directly.
- Scope snap index could unsigned-underflow → spurious dump. Guarded at arm + fire.
- `Serial.setTimeout(20)` so `readStringUntil()` in the `o`/`w` handlers can't stall
  the loop (which starved `pump()` and the input drain).

**PARKED — the serial ADC Scope dev tool.**
The scope graph does not work in the head firmware and is **deliberately shelved**,
not debugged further. Root cause is the **USB MIDI / USB-CDC-serial coexistence**
issue flagged in Step 0 §11: with `ARDUINO_USB_MODE=0` (TinyUSB owns USB for MIDI),
the serial control channel the scope relies on is not reliably bidirectional —
serial TX works (`[HIT]` lines appear) but host→device writes time out, so arming and
capture are unreliable. The app no longer freezes (it catches the write timeout and
warns), but the scope is non-functional under MIDI.
Rationale for parking: the scope was always a dev/advanced-only tuning aid, never
user-facing. Its purpose (observe signal + detector while tuning) only matters once
the **real** Stage 2 detector exists. Revisit from a larger architectural view in
Stage 2 — most likely as **raw capture over SysEx** (Step 0 §11), retiring the serial
path entirely. It's also possible the need is met by other channels (the `05 03`
hit-debug SysEx already works over MIDI) and a full scope proves unnecessary.

**Stage 2 — NEXT (the real detection core).**
Edrumulus-derived: band-pass IIR → square (power domain) → 3-segment decay-model
retrigger mask → clip/overload correction → first-peak vs max-peak. Designed to
**eliminate the hard-hit runaway** at the algorithm level (no watchdog band-aid).
Seed presets (PDX8/CY5/Lemon/KD8/PD8) gathered in Step 0 §7. Add "scope via SysEx
capture" as an explicit Stage 2 deliverable. Stage after: DUAL_PIEZO, then
PIEZO_SWITCH_CHOKE.

---

## Product Vision

**"Your sounds, your software, our hardware."**

BOAL BT-1 is a trigger interface, not a drum module. Serious drummers
already have better sounds in software (AD2, Superior Drummer, BFD) than
any hardware module ships with. BT-1 connects their kit to their existing
setup — beautifully designed, musician-first, no compromises.

Competitive framing:
- eDRUMin: powerful and affordable but technical and unglamorous
- Roland/Alesis: bundle mediocre sounds with trigger hardware to justify price
- BT-1: the well-designed, affordable trigger interface the market doesn't have

---

## BOAL Product Family

| Product | Description | Status |
|---------|-------------|--------|
| **BT-1** | Drum trigger interface, USB MIDI, desktop config app | Stage 1 active |
| **BT-1 Expand** | 4-input expansion board for BT-1 | Deferred (Phase 1B) |
| **BT-1 Screen** | ESP32-S3 + 5" capacitive touch config companion | Concept (Phase 1C) |
| **ClickBox** | Standalone click track box for drummers | Concept |

**BT-1 Screen details:**
- ESP32-S3 + 5" capacitive touchscreen, ~$90-100 AUD BOM
- Stacks on top of BT-1, separable via magnetic connector
- Connects via USB-C on mag connector (existing SysEx protocol,
  no new BT-1 firmware needed)
- Built-in LiPo charging — powers the BT-1 trigger unit wirelessly
- BLE for ClickBox song library sync
- Doubles as a ClickBox — one device, two jobs
- Purpose-built LVGL touch UI (not a PyQt6 port)
- Instant-on, low power, no OS
- Unified design language with BT-1 family; different form factor to
  ClickBox (ClickBox is body-worn, BT-1 family sits on desk/stand)
- Fusion 360 case design exploration planned

**Phase 2 (RPi standalone sound module) — dropped from active roadmap:**
At $600-700 AUD BOM it competes with a secondhand laptop that performs
better. BOAL's vision is trigger interface + user's existing software.

**ClickBox details:**
- Standalone click track box for drummers, completely hardware-independent
- nRF52840 (BLE), eInk display with frontlight (readable in low light),
  single NeoPixel LED on case for visual beat/section indication
- "You should be listening, not looking" — screen is for song/setlist
  navigation only; NeoPixel handles live beat/section feedback
- Web app for song building, setlist management, community library
- Song format: JSON tempo map + named sections + cue announcements
- NeoPixel behaviour configurable per song in web app
- Belt-clip, battery powered, different form factor to BT-1 family
- Concept stage as of June 2026

---

## Hardware (BT-1 Stage 1 — current dev unit)

- Custom PCB, Seeeduino XIAO footprint
- **ESP32-S3 now installed** (migrated from RP2040 as of 2026-06-28)
- MCP3008 SPI ADC removed — direct connections to ESP32-S3 internal ADC
  via jumper wires on PTH breakout pads (interim prototype only)
- GPIO2–9 → 4 dual-zone inputs (head + rim per jack)
- GPIO1 reserved for hi-hat controller (A0, not yet implemented)
- ADC front-end: 1kΩ series resistors + BAT85 clamp diodes + 1MΩ pull-down
  (22nF caps not yet fitted on interim board — target for next PCB spin)
- 4 stereo TRS jacks → 8 ADC channels (4 jacks, dual-zone capable)
  - **Tip = rim/switch channel; Ring = head/piezo channel** (corrected 2026-07-06 —
    see "Hard-won" note below; was previously documented backwards)
- 1 mono jack → GPIO1 directly (hi-hat controller, jack 4, stubbed)
- Stage 2: XIAO ESP32-S3 wireless satellite modules (architecture decided,
  PCB designed, not yet manufactured)

---

## Current Hardware Test Status (as of 2026-06-28)

Migrated to ESP32-S3 internal ADC. Threshold values require retuning
for new ADC noise floor (slider max raised to 500 in app).
Roland PD-7 confirmed triggering on new platform.
All four jacks active. USB MIDI working on Windows.

| Jack | Pad | Type | Status |
|------|-----|------|--------|
| 0 | Lemon 13" Cymbal | PIEZO_SWITCH_CHOKE | Previously working — needs retuning for ESP32-S3 ADC |
| 1 | Roland PDX-8 | DUAL_PIEZO | Previously working — needs retuning for ESP32-S3 ADC |
| 2 | Roland KD-80 | SINGLE_PIEZO | Previously working — needs retuning for ESP32-S3 ADC |
| 3 | Roland PD-7 | PIEZO_SWITCH_CHOKE | Confirmed triggering on ESP32-S3 — needs tuning |

---

## Working

- ESP32-S3 firmware boots cleanly, USB MIDI enumerates on Windows
- USB MIDI confirmed via MidiView — device name, note transmission working
- LittleFS config storage working on ESP32-S3
- SysEx protocol v0.2 — full read/write/save round-trip working
- Real pad triggering validated: Roland PD-7 confirmed on ESP32-S3
- TriggerEngine abstraction layer in place — PDrumTrigger implements interface,
  PDrum2Trigger stub ready for sensing rewrite drop-in
- Platform-conditional ring_buffer.h (RP2040 spinlock / ESP32-S3 FreeRTOS portMUX)
- Both [env:xiao_esp32s3_head] and [env:xiao_rp2040] build clean (0 warnings)
- Python app connects to ESP32-S3, reads/writes config, threshold slider now 0–500
- All previously working RP2040 features carry over (SysEx, LittleFS, app UI)

---

## App UI Architecture (current)

**Main window:**
- No top-level QTabWidget — PadConfigTab is set directly as central widget
- Clean single-view layout in user mode; no redundant tab chrome
- Dev mode: Presets Editor and Debug Console are floating QMainWindow
  instances launched from Dev menu (lazy creation, persist until app close)
- Dev menu items (dev mode only): Launch Emulator, Presets Editor…,
  Debug Console…
- closeEvent cleans up all floating windows
- Established pattern: dev tooling as optional floating windows from
  Dev menu — keeps main UI clean; apply to all future dev tools

**Left panel (top to bottom):**
- "INPUTS" section label
- 2×2 pad card grid (inputs 0–3)
- HLine separator ← expansion inputs insert here (inputs 4–7, deferred)
- HLine separator
- Hi-Hat Controller button (full width, 56px, icon + label, checkable)
- addStretch()
- AUTOTRACK button

**Right panel:**
- Velocity curve + hit log panels — full height, stretch=1, min 220px each
- Trigger settings sliders — stretch=0
- Detail tabs — stretch=0:
  - Config (index 0, default): Name + Type, Preset selector
  - MIDI (index 1): note assignments, channels, CC mapping, MIDI monitor
  - Options (index 2, disabled placeholder)
  - Advanced (index 3, disabled placeholder)
- Right panel stack: 0=placeholder, 1=pad detail, 2=hi-hat (placeholder)

**Interface modes:**
- Simple (default): clean pad grid, no dev tooling — musician-first
- Advanced: Dev menu unlocked, debug tools accessible
- Currently driven by --dev CLI flag
- Planned: persistent QSettings preference, switchable in Settings menu

---

## BOAL Design System

- Stylesheet: app/assets/styles/boal_base.qss (tokens + base widgets)
  + app/assets/styles/edrum.qss (product overrides)
- Loaded via app.setStyleSheet() in theme.py
- QPalette retained as fallback
- Token map at top of boal_base.qss — update hex values there AND in
  theme.py colour constants together
- Typography: IBM Plex Sans (UI) + IBM Plex Mono (numeric readouts)
  Currently falling back to Segoe UI — font bundling pending
- Colour palette:
  - bg-base #141414, bg-surface #1e1e1e, bg-card #252525
  - accent #00aabb (teal), accent-rim #cc6600 (orange)
  - warm text #d8d4ce, secondary #6b6b6b, disabled #3a3a3a
- No borders on inputs/combos/cards — differentiated by background shade
- Selected pad: teal icon recolour, no border
- Group boxes: borderless, 10px radius, uppercase spaced title
- Sliders: 4px groove, 12px round handle, filled track below handle
- Design system extends to all future BOAL products — boal_base.qss
  is the shared foundation

**SVG icon system:**
- app/assets/pads/ — SVG versions of all pad icons
- asset_loader.py: SVG preferred over PNG (tries .svg first, .png fallback)
- Runtime recolouring via QPainter CompositionMode_SourceIn
- Cache keyed on (name, size, colour_hex)
- SVG requirements: filled paths only (no strokes), transparent background
- Icon colours: normal #6b6b6b, selected #00aabb, reserved #3a3a3a

---

## Planned Dev Tools (Advanced mode only, floating windows)

- **Debug Console** — implemented; SysEx RX/TX monitor
- **Presets Editor** — implemented; manufacturer preset management
- **ADC Scope** — implemented (app/ui/scope_window.py); see ADC Scope section below

---

## ADC Scope Tool

Fully implemented dev-mode floating window (app/ui/scope_window.py).
Opened from Dev menu → ADC Scope…

**Architecture:**
- Serial connection (115200 baud, pyserial) to whichever COM port is selected —
  a generic port picker, not RP2040-specific (corrected 2026-07-11; this
  predates the ESP32-S3 migration and was stale). Unrelated to the WiFi telnet
  console path (`tools/telnet_logger.py`) used elsewhere in this project for
  live debugging — this is a separate, direct-serial transport.
- _SerialReader(QThread): decodes lines, delegates parsing to _ScopeParser
  (extracted 2026-07-11 so the same state machine also drives file loading —
  see "Load from log file" below), dispatches parser events to the existing
  signals unchanged.
- pyqtgraph chart: head (teal #2dd4bf) and rim (orange #fb923c) traces
- Overlays: Floor line, Trigger marker, Threshold line, Scan region, Mask region
- Session log: one row per capture, click to replot
- Serial output panel: all non-scope firmware output displayed live
- Serial input bar: send any command directly to firmware
- Load Settings button: sends 's' command, parses config for selected input,
  overlays threshold/scan/mask/retrig values on graph
- Arm/Disarm: single toggle button, teal when armed
- Auto-save: timestamped CSVs to app/logs/scope/
- Export CSV: full session log
- Copy from serial output: select lines + Ctrl+C or right-click → Copy

**Load from log file (added 2026-07-11):**
- "Load Log…" button in the connection bar, always enabled (works fully
  offline, no serial/device connection needed)
- Reads a tools/telnet_logger.py log file, strips the "[HH:MM:SS.mmm] "
  timestamp prefix telnet_logger.py adds to every line, feeds the rest through
  the same _ScopeParser used for live serial — not a separate implementation
- One log file can contain many captures (one per armed hit during that
  session); each becomes its own Session Log row using the log's own
  timestamps, exactly as if it had arrived live
- Malformed/partial captures (log copied mid-write, a hit-debug line
  interrupting a capture) are detected and skipped without crashing or
  corrupting other captures in the same file; skip count is reported in the
  Serial Output panel

**Firmware scope protocol:**
- 'o <input> <floor>' — arm scope on input, floor=noise gate
- 'o off' — disarm scope
- 'w <input> <param> <value>' — set DSP param live via serial
  params: thresh, sens, scan, mask, retrig
  (applies immediately via applyConfig() + deferred LittleFS save)
- Scope captures 200 samples: 100 pre-trigger + 100 post-trigger
- Trigger snapshot taken at threshold crossing (triggerSnap field in PDrum),
  not at scan end — gives accurate pre-trigger view of attack
- g_serialQuiet flag suppresses [HIT]/[RIM]/[SysEx] prints during ADC dump

**Key findings from scope sessions (CY-5 cymbal on input 0):**
- CY-5 head and rim piezos are strongly coupled — both channels activate
  on any strike. Amplitude ratio alone is insufficient for discrimination.
- Time-of-first-peak is a more reliable discriminator for this pad type:
  rim hit → orange leads; head hit → teal leads
- Cymbal resonance produces rhythmic oscillations lasting 20ms+
  after the initial strike — mask time needs to cover full decay
- Choke/grip signature is completely distinct from a hit:
  sustained plateau on rim channel, head stays at noise floor,
  slow rise/flat top vs sharp spike/fast decay of a hit
- PDX-12 mesh pad: head/rim channels very well isolated (10:1 ratio on
  head hits), amplitude discrimination works well for this pad type
- PD-7 rubber pad: very clean sharp transient, fast decay
- Current pdrum discrimination algorithm (difference-based) unreliable
  for CY-5 — ratio-based and/or time-of-peak approach needed

---

## Serial Debug Commands (firmware)

- h — print help + build number
- s — dump full config (all inputs, all DSP params)
- a — toggle continuous ADC channel dump (100ms interval)
- d — toggle [HIT]/[RIM] serial debug print (MIDI/SysEx output unaffected)
- m — toggle diagnostic mode (skips all detection + MIDI; pump + ADC dump only)
- o <input> <floor> — arm scope capture on input
- o off — disarm scope
- w <input> <param> <value> — set DSP param live (thresh/sens/scan/mask/retrig)
- p — send SysEx ping
- i — send SysEx identify request
- n — send test note (C3, ch10)
- r — reboot (ESP.restart())

---

## Protocol

- SysEx v0.2, manufacturer ID 00 7D
- Spec: docs/sysex_spec.md (authoritative)
- NUM_INPUTS = 5 (4 jacks + 1 hi-hat)
- INPUT_ID range: 00–04
- Link/unlink/input-status commands removed (02 08, 02 09, 02 0A)
- 57 Python self-tests passing

---

## Key Architecture Decisions

- One TriggerEngine instance per physical jack (not per ADC channel)
- One InputConfig per jack; z2note/z2channel = rim zone of same jack
- Tip = head/piezo channel; ring = rim/switch channel
- **Stage 1 and Stage 2 both use XIAO ESP32-S3** (decided 2026-06-28)
- **Wireless transport: ESP-NOW** (not BLE MIDI)
  - ~1–2ms latency, connectionless, satellites invisible to phones/computers
  - Pairing: broadcast-based handshake, MACs stored in LittleFS
  - Head unit is sole external gateway
  - Architecture: Config app ↔ USB MIDI SysEx ↔ Head unit ↔ ESP-NOW ↔ Satellites
  - SysEx protocol rides inside ESP-NOW packets unchanged
- Firmware: single shared codebase, compile-time flags
  (DEVICE_MODE HEAD_UNIT or SATELLITE), PlatformIO env targets
  [env:head_unit] and [env:satellite]
- Sensing abstraction: TriggerEngine abstract base → PDrumTrigger (current)
  → PDrum2Trigger (future). main_esp32s3.cpp uses TriggerEngine* array only.
- ADC: ESP32-S3 internal ADC, GPIO2–9 (ADC1 only — ADC2 conflicts with radio)
  Current: analogRead() on Core 0. Future: DMA continuous in PDrum2Trigger.
- Config storage: LittleFS binary structs; blob size tied to NUM_INPUTS
- Python venv: app/venv (Windows), ~/edrum-venv (Mac)
- Ring buffer: platform-conditional locking (RP2040 spinlock / FreeRTOS portMUX)
- USB MIDI init order (ESP32-S3): ARDUINO_USB_CDC_ON_BOOT flags must be
  removed from build_unflags/build_flags — leave at board defaults

---

## Default DSP Values

- threshold: 30, headSensitivity: 500, scanTime: 10, maskTime: 30
- rimThreshold: 30, rimSensitivity: 200, velocityCurve: 0 (linear)
- midiChannel: 10, zone2MidiChannel: 10
- Jack 0: note=36 (kick), z2=36
- Jack 1: note=38 (snare head), z2=40 (snare rim)
- Jack 2: note=42 (hi-hat closed), z2=46 (hi-hat open)
- Jack 3: note=51 (ride), z2=53 (ride bell)
- Jack 4: note=44 (hi-hat pedal CC), stubbed

---

## Pad Type Architecture (settled design — drives firmware + app)

Three distinct pad types identified from waveform analysis across all
tested pads. Each requires structurally different sensing logic.

### DUAL_PIEZO (value: 0)
Pads: Roland PDX-8, PDX-12
- Head: piezo (tip channel), Rim: piezo (ring channel)
- Both zones produce velocity-sensitive analog signals
- Discrimination: ratio-based (rimPeak/headPeak) + time-of-first-peak
  as tiebreaker for ambiguous soft hits (PDX-8 soft rim = 1.3:1 ratio)
- Hard head hit: head:rim ~10:1. Hard rim hit: rim:head ~3.8:1
- Two independent MIDI notes (head note + rim note)
- UI: full parameter set — rimRatioThreshold, z2note, z2channel visible

### PIEZO_SWITCH_CHOKE (value: 1)
Pads: Roland CY-5, Roland PD-7, Lemon Cymbal, Lemon Ride
- Head: piezo (tip channel), Rim: mechanical switch (ring channel)
- KEY INSIGHT: for cymbals/pads with a switch, the rim is NOT a second
  zone — it is a CHOKE CONTROL. In standard MIDI percussion, bow and
  edge of a cymbal map to the same note. The switch mutes the sound.
- Switch channel sensing is completely different from piezo sensing:
  - NOT peak scanning — monitoring for sustained signal above threshold
  - Choke = signal stays elevated >5ms (slow rise, flat top plateau)
  - Hit-induced switch transients (brief spikes) = ignored
  - Choke action = MIDI note-off for this input's current note
- Choke signature confirmed across CY-5, Lemon Cymbal, PD-7 via scope
- Switch threshold varies by pad (CY-5 ~78, Lemon ~33, PD-7 ~105-185)
  — chokeThreshold must be configurable
- UI: hide rimSensitivity, rimThreshold, z2note, z2channel
  Show: chokeEnabled (bool toggle), chokeThreshold (slider)

### SINGLE_PIEZO (value: 2)
Pads: Roland KD-80
- Head: piezo (tip channel) only, no rim sensor
- No zone discrimination logic at all
- KD-80 bounce vs bury visible in waveform (future: technique detection)
- UI: hide all rim/choke parameters

### Observed parameter ranges from scope data
| Parameter      | Current default | Recommended range | Notes                           |
|---------------|----------------|-------------------|---------------------------------|
| threshold     | 30             | 10–100            | Noise floor ~5-10 ADC units     |
| sensitivity   | 500            | threshold–1023    | Hard hits reach 1023; 500 wastes|
|               |                |                   | top half of velocity range      |
| scanTime      | 10ms           | 1–10ms            | All peaks within 3ms of trigger |
| maskTime      | 30ms           | 10–150ms          | Cymbal 80-100ms, mesh 40ms,     |
|               |                |                   | rubber 20ms, kick 50ms          |

### Starting preset values per pad
| Pad           | Type                | thresh | sens | scan | mask | chokeThresh |
|--------------|---------------------|--------|------|------|------|-------------|
| Roland CY-5  | PIEZO_SWITCH_CHOKE  | 20     | 800  | 3    | 80   | 50          |
| Lemon Cymbal | PIEZO_SWITCH_CHOKE  | 20     | 800  | 3    | 80   | 20          |
| Lemon Ride   | PIEZO_SWITCH_CHOKE  | 20     | 800  | 3    | 80   | 20          |
| Roland PD-7  | PIEZO_SWITCH_CHOKE  | 20     | 800  | 3    | 20   | 80          |
| Roland PDX-8 | DUAL_PIEZO          | 20     | 800  | 3    | 40   | —           |
| Roland PDX-12| DUAL_PIEZO          | 20     | 800  | 3    | 40   | —           |
| Roland KD-80 | SINGLE_PIEZO        | 20     | 800  | 3    | 50   | —           |

---

## Retrigger-Cancel Design Spec (v2, agreed 2026-07-07; revised 2026-07-08
after hardware validation)

**Status: IMPLEMENTED AND HARDWARE-TESTED 2026-07-07/08 — v2's core mechanism
works as designed, but hardware testing surfaced a real structural gap requiring
the revisions below before it's usable.** Do not re-implement the core
peak/trough state machine (it's correct, see Hardware Validation Findings) —
the changes needed are: (1) make retrigger-cancel opt-in per input via
`retrig=0`, not always-on, and (2) cap the seed value below the ADC ceiling.
Both are additive changes to the existing v2 implementation, not a redesign.

### Hardware validation findings (2026-07-08)

Tested on both the PDX-12 (mesh, oscillating decay) and PD-7 (rubber, clean
decay) with full `[RCEXIT]` visibility:

- **PDX-12: works well.** Real hits produced fast natural exits (`dur=16ms`,
  `dur=0ms` in testing — nowhere near the 900ms hard cap), confirming the
  peak/trough reversal logic correctly recognises this pad's genuine
  oscillating decay and ratchets `lastConfirmedPeak` down quickly. One
  separate, real issue surfaced in the same test (see below) — not a
  retrigger-cancel bug.
- **PD-7: found two compounding structural problems, not implementation bugs.**
  A moderate strike produced `[RCEXIT] exit=1 lcp=4091 dur=902ms` — i.e.
  `lastConfirmedPeak` NEVER MOVED from its seeded value for the entire 900ms
  monitor window, only ending via the hard cap.
  1. **Root cause: PD-7's decay is smooth/monotonic, so it never produces a
     confirmed peak/trough reversal at all.** Traced step by step: Mask ends →
     MONITOR starts in FALLING, tracking the running minimum as the signal
     smoothly decays. Since a smooth decay never rises, the
     FALLING→RISING confirmation ("signal rises more than `margin` above the
     running minimum") never fires — so the RISING→FALLING confirmation that
     ratchets `lastConfirmedPeak` down can never fire either. The reference
     is stuck at the original seed value until the hard cap forces an exit.
     **This generalises beyond "smooth decay" specifically: it happens
     whenever a pad's real decay oscillation amplitude is smaller than
     `margin`** — including a genuinely oscillating pad if `margin` is set
     larger than that pad's real wobble size.
  2. **`truepeak=4091` (this hit clipped the 12-bit ADC, ceiling ~4095).**
     With `lastConfirmedPeak` frozen at 4091, the new-strike accept bar for
     the whole 900ms was `4091 + margin` — a value the hardware can never
     physically produce. **No strike, however hard, could register as a new
     hit during that window.** This is a distinct, compounding problem on
     top of (1): it would still occur even on a pad with genuine oscillation,
     any time the initiating hit clips the ADC (a normal occurrence on a
     firm-to-hard hit, not a rare edge case).
- **Attempted fix explored and rejected: lowering `margin` to catch smaller
  oscillations.** Traced through explicitly and found to be a genuine dead
  end, not just a tuning trade-off: a `margin` small enough to confirm a real
  but subtle oscillation is also small enough for ordinary sample noise to
  trip the same FALLING→RISING→FALLING confirmation logic — and because noise
  churns constantly (unlike the pad's real decay timeline), this can ratchet
  `lastConfirmedPeak` down to near the noise floor almost instantly, firing a
  premature natural exit **while the pad is still genuinely ringing at real
  amplitude** — reopening the exact false-retrigger problem this mechanism
  exists to solve, via a different path. `margin` cannot be tuned small enough
  to solve the PD-7 case without this cost; there is no single value serving
  both jobs (noise rejection vs. oscillation detection) for a pad whose real
  oscillation is close to its own noise-floor size.

### Retrigger-cancel is opt-in per input, not universal (revised 2026-07-08)

**Re-reading Roland's own TD-3 manual wording resolved the above tension by
dissolving it rather than finding a clever `margin` value:** "*Important if you
are using acoustic drum triggers.* Such triggers can produce altered
waveforms..." — explicitly conditional language, describing a specific class
of pad (complex/distorted decay), not a universal requirement. This matches
the hardware findings exactly: the PDX-12 has the kind of decay this feature
is for; the PD-7 doesn't need it and Scan+Mask+Threshold already handle it
correctly on their own (confirmed working for the PD-7 well before v2 existed).

**Mechanism: `retrig=0` (the existing repurposed field) means "retrigger-cancel
disabled for this input," not "margin=0."** This must be an explicit code path,
NOT a mathematical consequence of margin=0 — plugging 0 into the existing
formulas makes the system maximally *aggressive* (any 1-count rise trips both
checks instantly), the opposite of "off". Required behaviour: when `retrig==0`,
`serviceMask()`'s hand-off skips `MONITOR` entirely and returns straight to
`IDLE` — i.e. exactly the pre-v2 Scan→Mask→Idle behaviour, unchanged.
**Fresh-install default changes to 0 (off)** — matches the finding that most
tested pads (PD-7, KD-80) don't need this feature; it's something a person
opts a specific (mesh-type) pad into, not a universal default.

### Seed cap for ADC-ceiling clipping (new, 2026-07-08)

For inputs that DO have retrigger-cancel enabled, separately fix problem (2)
above: seed `lastConfirmedPeak` as `min(velocityRaw, capValue)` rather than
`velocityRaw` directly, where `capValue` sits comfortably below the ADC
ceiling (~4095) with enough headroom that `capValue + margin` stays physically
achievable. `capValue` needs a real placeholder now and proper calibration
later (same recorded-waveform methodology as `margin` — see Open Items),
not a guess treated as tuned.
**This recovers real, meaningful functionality**: a genuine second hit that's
moderate-to-hard but does NOT itself clip can now clear the (capped) accept
bar and register correctly — this is the common case for most real second
hits in a roll or accent.
**It does not, and cannot, fully solve the clipped-vs-clipped case** — see
Known Limitations below, this is a fundamental information limit, not
something a cap value can tune around.

### Known limitations, accepted deliberately

1. **A genuine second strike quieter than wherever the decay currently sits is
   indistinguishable from a decaying residual peak using peak level alone**
   (original v2 limitation, unchanged) — both look like "a rise that doesn't
   clear margin." Judged a fundamental limit of amplitude-based
   discrimination, not unique to this design (TD-3's own wording doesn't claim
   to solve this either). The peak-to-previous-peak (not peak-to-original)
   comparison narrows this: a genuine second hit only needs to clear wherever
   the decay currently sits, not the original strike's full level.
2. **Clipped-vs-clipped (new, 2026-07-08): if BOTH the initiating strike and a
   genuine close second strike clip the ADC, they read identically (~4095),
   and the seed cap above cannot distinguish them** — the amplitude
   information that would tell them apart was destroyed by clipping itself,
   not lost due to how the cap was chosen. **Concrete musical scenario: a hard
   flam** (two full-force hits close together, but far enough apart to clear
   Mask) **on a pad whose decay is being monitored — the second hit may not
   register.** Checked and ruled out: using attack-rate to break the tie (the
   technique that worked for the original v1 machine-gun problem) does NOT
   help here — that worked because it compared a genuine strike's attack
   shape against a *different physical process* (the pad's own rebound); here
   we'd be comparing one genuine strike's attack against another genuine
   strike's attack on the same pad, which have no reason to differ (mesh heads
   in particular have an inherently slower attack than rubber/cymbal, due to
   membrane flexibility, but that's constant across hits, not a discriminator
   between them). **Accepted as a known trade-off** — the only theoretical
   full fix is hardware that doesn't clip in the first place (see Future
   Hardware Idea below), which is out of scope for the current PCB revision.

### Future hardware idea: dual-gain head-channel sensing (not in scope now)

Raised while discussing the clipped-vs-clipped limitation, worth recording for
the satellite module design specifically (2 jacks per satellite vs. 4 per head
unit — spare ADC channels available where the head unit has none):
- Tap the same piezo signal into TWO ADC channels: one unchanged
  (high-gain/sensitive), one through an added series resistor forming a
  voltage divider with the existing pull-down (low-gain/robust — attenuated
  enough that no realistic hit, however hard, clips it).
- Firmware: if the high-gain channel isn't clipped, use it directly (best
  resolution). If it IS clipped, fall back to the low-gain channel's reading,
  scaled back up by the known divider ratio — recovering a true-amplitude
  estimate that a single clipped channel cannot provide.
- This would fully solve the clipped-vs-clipped limitation above, IF sized
  with enough low-gain headroom that no real hit ever clips that channel too.
- **Rim does not need this** — rim is a discrimination signal (not something
  we're extracting fine velocity nuance from) and isn't the channel driving
  the retrigger-cancel/decay-oscillation problem.
- Real design work needed before building this: the attenuation ratio has to
  be chosen from real hit-amplitude data (same methodology as margin/capValue
  calibration), not guessed — too little added headroom doesn't help, too much
  loses resolution in the "moderate hit" crossover zone.
- Not started; recorded here so it isn't lost, not an active task.

### The three-phase model

Hit detection is three distinct phases, each solving a different problem.
They are complementary, not overlapping — confirmed explicitly against the
TD-3 manual's own Mask Time vs Retrigger Cancel distinction:

1. **Scan** (existing, unchanged) — threshold crossing starts a fixed-duration
   window; the running max seen during that window becomes the hit velocity.
2. **Mask** (existing, unchanged) — a flat, short, user-tunable blackout window
   immediately after scan ends. Purpose: suppress **genuine physical re-contact**
   that's real but musically unwanted — e.g. a kick beater bouncing back and
   striking again, or a stick not rebounding cleanly. Per Roland's TD-3 manual:
   "the beater can bounce back and hit the pad a second time immediately after
   the intended note... Mask Time does not detect trigger signals if they occur
   within the specified time after the previous trigger." This is a drummer/
   mechanism-side problem — the tuning goal is "as short as possible while still
   preventing bounce-back," a musical choice, unaffected by anything below.
3. **Retrigger-cancel** (NEW, this spec) — starts the instant Mask ends. Purpose:
   distinguish a pad's own decay/oscillation (real signal, but not a new strike)
   from a genuine new impact. This is a pad-physics-side problem, structurally
   different from what Mask solves, and needs a different mechanism because the
   relevant timescale (pad decay, confirmed up to ~600-700ms on the PDX-12) is
   far too long for a flat blackout window without destroying playability.

### Retrigger-cancel mechanism

Two processes run concurrently, every sample, from the moment Mask ends:

**(a) New-strike check — fast, unconditional, no debounce:**
```
if (rawSignal > lastConfirmedPeak + margin):
    genuine new strike detected
    cancel retrigger-cancel monitoring
    start a fresh Scan window immediately, from this sample
```
No confirmation delay needed here — `lastConfirmedPeak` is already a trusted
reference, so clearing it by a real margin is decisive on its own. This is also
what gives correct velocity for a second strike: it's captured by its own fresh
Scan window, not estimated from the retrigger-cancel logic.

**(b) Reference tracking — debounced, ratchets `lastConfirmedPeak` downward as
decay progresses:**
```
state: FALLING or RISING

FALLING: track running minimum.
  Once signal rises more than `margin` above that minimum → confirmed trough,
  switch to RISING.

RISING: track running maximum.
  Once signal falls more than `margin` below that maximum → confirmed peak.
  Since (a) never fired, this peak is <= lastConfirmedPeak, i.e. genuinely
  smaller — update lastConfirmedPeak DOWN to this new value.
  Switch back to FALLING.
```
The `margin` requirement in both (a) and (b) is a single, shared parameter for
now (see "Open items" below) — it exists specifically to reject small spurious
rises from being mistaken for a genuine new peak/trough, whether those rises
come from ordinary sample noise OR from a pad's decay being genuinely
non-uniform (mesh heads in particular can wobble/fluctuate slightly during
decay rather than falling smoothly — this is part of what TD-3's manual is
describing when it mentions "altered waveforms" at the decaying edge). Treating
both causes with the same margin-based prominence rule is deliberate: from the
detector's point of view they're indistinguishable, and both need the same
answer (ignore small wobbles, react to large ones).

**Continuity at phase start:** `lastConfirmedPeak`'s initial value, at the exact
moment retrigger-cancel begins (Mask just ended), is the Scan window's own
recorded max — no separate seeding logic needed.

**Exit conditions (two, not one):**
- **Natural exit:** once `lastConfirmedPeak` has decayed down near the noise
  floor / `headThreshold`, retrigger-cancel and plain threshold detection become
  functionally identical — exit early rather than waiting out a fixed timer.
- **Hard safety cap** (~800ms–1s, sized generously above the slowest decay
  measured so far — the PDX-12's ~600-700ms rebound gaps, with real headroom):
  guarantees the system always returns to normal state even if natural exit
  can't cleanly fire. Concretely needed for pads like the KD-80 with a bouncy
  beater providing sustained, irregular low-level contact — `lastConfirmedPeak`
  may never cleanly settle near the noise floor in that case, so the timer is
  the only guaranteed way out.
  A fixed, generous cap costs nothing on fast pads (rubber etc.) since natural
  exit fires well before the cap is ever relevant there — it only acts as a
  backstop for the pads that actually need it.

### What's user-tunable, and what changed as a result

- **`margin` is the real tunable parameter** — not a time length. This is a
  genuine simplification enabled by the natural-exit design: since exit
  duration adapts automatically to how long each specific pad's decay actually
  takes, a separate "retrigger length" setting per pad type is no longer
  needed — only `margin` needs characterising per pad type.
- The hard safety cap is a **fixed, non-user-facing constant**, not something
  to expose — it's a backstop, not a musical control.
- Starting with **one shared margin** for both (a) and (b) above, per pad — not
  split into two separate values — until real data shows a reason to separate
  them.

### Known limitation, accepted deliberately

A genuine second strike that happens to be **quieter** than wherever the decay
currently sits is indistinguishable from a decaying residual peak using peak
level alone — both look like "a rise that doesn't clear margin." This is judged
to be a fundamental limit of amplitude-based discrimination (not a flaw unique
to this design — TD-3's own "weeding out false trigger signals" wording doesn't
claim to solve this either), and is accepted as a trade-off rather than solved
here. The upside of the peak-to-previous-peak (not peak-to-original-strike)
comparison: a genuine second hit only needs to clear wherever the decay
currently sits, not the original strike's full level — so this limitation is
narrower than it could be.

### Open items for implementation / next steps

- **Implement the two 2026-07-08 revisions** (retrig=0 opt-out path, seed cap)
  against the existing v2 state machine — additive changes, not a rewrite.
- **Margin calibration methodology** — almost certainly needs real recorded-
  waveform data per pad type (mesh especially, now that scope is narrowed to
  pads that actually need this feature), not a guessed constant. Natural home
  for the previously-filed "recorded-waveform-driven decay/mask tuning"
  future-phase idea (see Pending section).
- **`capValue` (seed cap) also needs real calibration**, same methodology,
  once the mechanism itself is confirmed working with a placeholder.
- Whether the `peakSampleIdx` (attack-rate) concept has any remaining role as
  a secondary confirmation signal was raised, then specifically re-examined
  for the clipped-vs-clipped case and found NOT to help there (see Known
  Limitations #2) — considered closed for now, not just deferred.

---

## Scan Redesign + EMA Smoothing + Tunable Constants (v3, agreed 2026-07-12)

**Status: IMPLEMENTED AND HARDWARE-VALIDATED 2026-07-12 — genuinely working
trigger module.** Tested across three pads simultaneously (PDX-8 mesh, CY-5,
PD-7 rubber): **no audible double-triggering, no discernible missed hits.**
This is the first session where the whole system played like a real trigger
module end-to-end, not just individual mechanisms tested in isolation.

### Hardware validation findings (2026-07-12)

- **`[CHOKE]` flood immediately after `uploadfs` — expected side effect of the
  reset, not a bug.** `PDrumTrigger`'s constructor default is `padType=1`
  (PIEZO_SWITCH_CHOKE); a fresh config reset put every input back on that
  default, including jack 0 (PDX-8, which needs `type=0` DUAL_PIEZO). Under
  `type=1` the rim channel is read as a choke switch, so the PDX-8's genuine
  rim piezo signal repeatedly registered as sustained choke contact. Fixed
  with `w 0 type 0`. **If this recurs after any future `uploadfs`, check
  `padType` first before assuming a new bug** — it's the constructor default
  reasserting itself, every time.
- **`scanexit=1` (hard-cap) fires broadly across the strength range, not just
  on extreme/clipping hits — confirmed across a full soft-to-hard sweep on
  the PDX-8.** Only the very softest hits (`truepeak` 24-90) reliably settle
  before the cap; from roughly `truepeak=272` upward, hard-cap dominates,
  with `confirms` often 8-10 — i.e. the signal is genuinely producing that
  many separate confirmed local peaks in quick succession, each resetting the
  5ms settle timer, right up until the 30ms cap forces a decision. **Not a
  correctness bug** — the safety-net floor (`max(scanHeadBest_,
  scanHeadTrk_.runMax)`, verified in code review) means the reported velocity
  is still correct throughout (confirmed sensible values across the whole
  sweep) — but it means Scan is spending close to the full hard-cap duration
  on most non-trivial PDX-8 hits, relying on the backstop as the *normal*
  path rather than the rare case it was designed for. Currently inaudible
  (system plays great), but a real, specific latency characteristic —
  `scanMargin` is the likely lever if this ever needs tightening (see Open
  Items). Not urgent given how well it's playing right now — recorded so it's
  understood, not chased further this session.

### Implementation summary (for reference; see git history for full detail)

Built in `PDrumTrigger.cpp/.h`: shared `ExtremumTracker` primitive (used by
both the unchanged MONITOR ratchet-DOWN and the new Scan ratchet-UP, proven
equivalent to the old inline MONITOR logic via a 5000-sequence differential
port test); confirmation-based Scan on all three pad types (DUAL head+rim
independently tracked, head-driven exit); EMA smoothing after spike-rejection
(confirmed in code review — spike-rejection's history capture genuinely
precedes the EMA reassignment); new telnet-`w` tunables `scanmargin`/
`settlewait`/`ema` (raw `InputConfig` fields, NOT in SysEx; `scan` repurposed
as Scan's hard-cap ms, default 30); `kRetrigSeedCap` reverted 4095→3500
(was a temporary diagnostic override, see the Retrigger-Cancel section).
Diagnostics: `scanexit`/`confirms`/`scandur` on `[HIT]`/`[RIM]`.

### Open items

- `scanMargin` tuning to reduce hard-cap reliance on the PDX-8 (see Hardware
  Validation Findings above) — optional refinement, not currently a problem
  a listener notices; revisit only if it becomes worth chasing.
- Whether `scanMargin` ends up needing its own value or can share `retrig`'s
  margin is still explicitly UNRESOLVED (only PDX-8 has been characterized in
  depth for Scan specifically).
- `settleWaitMs` and `emaAlpha` starting values remain placeholders — real
  per-pad-type calibration is still an open, deferred task (same methodology
  as `margin`/`capValue`).
- Original full design rationale (why confirmation-based ratchet-UP was
  chosen over Edrumulus's FIFO/pre-scan approach, the EMA-vs-band-pass
  trade-off reasoning, pipeline ordering reasoning) preserved below for
  reference — all still accurate, no changes needed after hardware testing.

**Original status: DESIGNED, NOT YET IMPLEMENTED.** Grew out of testing the retrig=0/
seed-cap revisions above on the PDX-8: found `scan=1` (and `scan=0.8` rounded
to 0/1) badly undercounting some real hits — one moderate strike's true peak
(1849) didn't arrive until ~40 samples (~5ms) after threshold crossing, while
`scan=1` (~8 samples) locked in ~491 instead. Root cause identified: **this
pad's attack SPEED varies with hit force** — hard/clipping hits peak in 2-3
samples, moderate hits can take 5ms+. No single fixed `scantime` can be
correct for both. Separately, investigating why the PDX-8 consistently
produced exactly 3 registered hits per strike (confirmed independent of the
seed-cap value via a live test — varying the seed 3500→4095 made zero
difference to the "always 3" pattern, ruling that theory out) led to reading
corrados/edrumulus's actual algorithm docs, which independently confirmed:
band-pass-filtered piezo signals characteristically show **three distinct
peaks "no matter what the original peak looks like"** — this is a known,
general property of piezo signals, not a pad-specific mystery.

### Confirmation-based Scan (replaces fixed-window Scan)

**Core idea, agreed after evaluating and rejecting Edrumulus's FIFO/pre-scan
approach as more machinery than needed:** rather than buffering samples and
searching backward for the true peak (Edrumulus's method), make Scan
structurally the mirror image of the already-working Monitor mechanism —
ratchet **UP** toward the true peak instead of ratcheting **DOWN** as decay
progresses. Reuses the same margin-debounced local-extremum-confirmation
primitive Monitor already uses; implement as ONE shared helper used by both
(ratchet direction as the only difference), not duplicated logic.

Mechanism:
1. Threshold crossing starts Scan (unchanged). Track a running candidate max.
2. Once the signal falls more than `scanMargin` below that candidate → this
   local max is CONFIRMED (same debounce as Monitor's peak/trough tracker).
   Do NOT lock in and report yet — set it as the current best-confirmed-peak
   and keep watching (this is the key difference from a naive "confirm once
   and stop" version — needed specifically because Edrumulus found the
   SECOND peak in the three-peak structure can be bigger than the first;
   locking in on first confirmation would sometimes report the smaller one).
3. If a NEW confirmed local max exceeds the current best → ratchet the
   best-confirmed-peak UP to that new value (mirrors Monitor's downward
   ratchet exactly, just inverted).
4. **Settle exit:** once no new, higher confirmed peak has appeared for
   `settleWaitMs` → commit the current best-confirmed-peak as final velocity,
   transition to Mask.
5. **Hard cap:** generous backstop duration so Scan can never hang
   indefinitely on a pathological signal — same safety role as Monitor's
   900ms cap, sized for Scan's much shorter real timescale.

**`scantime`'s OLD meaning (fixed window duration) is retired — repurposed as
Scan's hard-cap**, same pattern as `retrig` being repurposed for Monitor's
margin. `scanMargin` is a genuinely NEW, separate parameter from `retrig`/
Monitor's margin — explicitly NOT assumed to be the same value: attack rate
and decay rate are different physical processes and may need different
margins; may turn out one shared value works, may not — real data needed
before concluding either way, not decided in the abstract.

### EMA smoothing (approximates band-pass filtering, stays in raw ADC units)

Investigated whether a proper band-pass filter (Edrumulus uses 40-400Hz) is
worth adding. Conclusion: **not now** — real costs (filter delay needing
compensation, and critically, it changes the signal's scale/shape enough that
EVERY threshold/sens value tuned so far this week would need re-tuning, not
just nudging). Instead: a band-pass filter is really a high-pass + low-pass
stacked. **We already have the high-pass half** — DC-offset removal (the
existing slow one-pole IIR) mathematically IS a high-pass filter, already
blocking slow baseline drift. Adding a simple **EMA (exponential moving
average)** provides the missing low-pass half, attenuating ordinary
sample-to-sample ADC jitter (the noise-floor phenomenon measured repeatedly
this project, ~4-17 counts). Together, DC-offset + EMA approximates a
band-pass filter using only simple, already-trusted operations —
**deliberately chosen over a proper filter for lower implementation risk,
per this project's "simple version first, refine from data" working
philosophy.**
**Critically: both operations are plain subtract/average within the existing
0-4095 raw ADC domain — no rescaling.** Every `thresh`/`sens`/`margin`/
`scanMargin` value tuned this week stays meaningful. This was explicitly
identified as the deciding advantage over a proper band-pass filter.

**Pipeline order (decided): DC-offset removal → spike-rejection (existing,
unchanged) → EMA (new).** Reasoning: spike-rejection specifically targets
large single-sample outliers (the `SPIKE_THRESHOLD=200` mechanism); if EMA
ran BEFORE spike-rejection, a genuine large glitch would get smeared across
several samples by the EMA's own averaging instead of being cleanly caught
and rejected as one bad sample. Running spike-rejection first on the more-raw
signal, then EMA afterward on what's left, avoids this.

**Starting alpha: ~0.5** (roughly equal weight on current sample vs. running
smoothed value — effective time constant ~1-2 samples). Reasoning: must stay
SAFELY FASTER than the fastest real attack measured (2-3 samples on a hard
clipping hit) or the EMA would blunt exactly the fast transients the new
confirmation-based Scan needs to see accurately — working against the very
thing just designed. This is a REASONED starting point, not a validated one
(same category as `kRetrigSeedCap`'s placeholder) — first real test is the
same `a` (ADC dump) before/after comparison used for every other noise-floor
measurement this project.

### Tunable constants — telnet `w`, not the config file, as the fast-iteration path

Given this phase is active algorithm tuning/building (not pad tuning), and
given `w <input> <param> <value>` already works well for exactly this (used
for `retrig` extensively this week) — new algorithm constants
(`scanMargin`, `settleWaitMs`, `emaAlpha`) should be added the same way:
new fields settable live via `w`, persisted in `InputConfig`/LittleFS like
existing fields, taking effect immediately without a reboot.
**Explicitly NOT wired into the SysEx protocol for now** — telnet-only,
same bootstrap pattern `retrig` itself followed before any UI work existed
for it. Avoids protocol/app changes while these values are still being
found, not yet validated enough to expose to end users.

### Open items

- Whether `scanMargin` ends up needing its own value or can share `retrig`'s
  margin is explicitly UNRESOLVED — build both as independent parameters,
  let real testing (not reasoning) decide if they converge.
- `settleWaitMs` and `emaAlpha` starting values are placeholders (see reasoning
  above) — same calibration-later treatment as `margin`/`capValue`.
- Real risk flagged, not yet tested: could add latency (however small) even to
  pads that already work well (PD-7, CY-5) if their fast, clean attacks now
  wait out a settle window before committing. Needs checking once built —
  don't assume it's fine.

---

## pdrum Library — Rewrite Plan

Current difference-based algorithm is unreliable across all tested pads.
Full rewrite with three separate sensing code paths required.

**New InputConfig fields needed:**
- `padType`: uint8_t (0=DUAL_PIEZO, 1=PIEZO_SWITCH_CHOKE, 2=SINGLE_PIEZO)
- `rimRatioThreshold`: uint16_t (scaled integer, DUAL_PIEZO only —
  replaces rimSensitivity; ratio = rimPeak*100/headPeak, threshold ~40)
- `chokeThreshold`: uint16_t (ADC units, PIEZO_SWITCH_CHOKE only)
- `chokeEnabled`: bool

**Parameters to retire:** rimSensitivity (replaced by rimRatioThreshold),
rimThreshold (absorbed into chokeThreshold)

**Sensing logic per type:**

DUAL_PIEZO:
1. Spike rejection + peak tracking both channels during scan window
2. Track which channel first exceeded threshold (firstPeakChannel)
3. At scan end: ratio = rimPeak * 100 / headPeak
4. RIM if ratio > rimRatioThreshold OR (ratio > 80 AND firstPeak==rim)
5. HEAD otherwise

PIEZO_SWITCH_CHOKE:
1. Head channel: standard peak detection → hit + velocity (head note only)
2. Switch channel: sustained signal monitor (NOT peak scan)
   - Track consecutive samples above chokeThreshold
   - If sustained >~5ms → set chokeDetected flag
   - Core 0 reads flag → sends MIDI note-off for this input
3. No rim MIDI note output

SINGLE_PIEZO:
1. Head channel only, standard peak detection
2. No rim/switch logic

**Legacy cleanup in same pass:**
- Remove unused HelloDrum members (exTCRT, exFSR, pedalCC, hi-hat flags,
  padtype[]/instrumentName[] arrays)
- Replace curve() pow() with lookup table
- Remove else-if(0) choke dead code

**Note:** Changing InputConfig struct requires LittleFS filesystem
re-upload (platformio run --target uploadfs) — config will reset to
defaults. Expected and correct behaviour.

---

## pdrum Library — Known Gaps (next major task)

See Pad Type Architecture and pdrum Rewrite Plan sections above for
the full picture. Summary of remaining gaps:
- No watchdog timer integration
- Unused HelloDrum legacy members (cleanup in rewrite)
- curve() pow() — replace with lookup table (in rewrite)
- HelloDrum reference: github.com/RyoKosaka/HelloDrum-arduino-Library

---

## Known Issues / Gotchas

- Windows WinMM: rtmidi callback silently drops SysEx — use polling
- Windows WinMM: echoes sent SysEx back to input — echo filter in transport
- RP2040 upload: 'r' serial command → bootloader, or picotool
- LittleFS uploadfs requires mklittlefs in PATH:
  C:\Users\andre\.platformio\packages\tool-mklittlefs-rp2040-earlephilhower
- Changing NUM_INPUTS or InputConfig struct requires re-uploading filesystem
- Mac SSH sessions cannot receive MIDI (no CoreMIDI run loop)
- BLE MIDI SysEx dropped by macOS Monterey (use USB)
- PlatformIO build script: use env["PROJECT_DIR"] not __file__ (SCons context)
- version.txt must not be empty — must contain an integer
- MCP filesystem server: home desktop still points at old Dropbox path —
  needs updating to D:\Dev\eDrum\edrum-project\ after migration
- **[RESOLVED 2026-07-06] Hard hit runaway:** On very hard hits (mainly mesh pads),
  the unit occasionally entered a runaway state firing continuous MIDI notes.
  Usually cleared by hitting the pad again; occasionally required USB replug.
  This was the exact same underlying bug in both the old pdrum-era code AND its
  eventual PDrumTrigger reincarnation — see the "PDrumTrigger spike-rejection
  self-lock" entry under Hardware/debugging constraints above for the confirmed
  root cause and fix (a self-poisoning history bug in the spike-rejection filter,
  not ADC saturation or a missing loopTimes watchdog as originally suspected here).

---

## Pending — Next Sessions

**[DONE 2026-07-12] Scan Redesign v3 + EMA + Tunable Constants + the two
2026-07-08 retrigger-cancel revisions.** All implemented, hardware-validated
across 3 pads (PDX-8, CY-5, PD-7), no audible double-triggering or missed
hits. See "Scan Redesign + EMA Smoothing + Tunable Constants (v3)" section
above for full detail and remaining minor open items (scanMargin tuning,
margin/capValue calibration — none urgent).

**Immediate — pad-specific tuning and setup (next focus, 2026-07-12):**
General triggering behaviour is now considered "good enough" — shift focus
from algorithm work to per-pad calibration (thresh/sens/margins) using the
methodical characterization process established earlier this project
(noise floor → dynamic range sweep → curve check → rim discrimination →
choke calibration → mask/retrigger tuning), now with the ADC Scope's
load-from-log-file + Config-block overlay tooling available to make this
visual rather than reading raw numbers.

**Future — recorded-waveform-driven decay/mask tuning (added 2026-07-06, do not
start until the pdrum-revival Phase 1 + Phase 2 below are independently confirmed
working on hardware):**
- Idea, from re-examining Edrumulus's own documented methodology (record real test
  signals per pad type, tune the decay/mask model against them) — the *approach* is
  good even though we're moving away from Edrumulus's specific C++ implementation.
  Matches what scope sessions already showed by eye (rubber pad PD-7: sharp spike,
  fast decay; mesh/cymbal pads: long oscillating decay tail) — worth quantifying
  properly instead of just eyeballing a serial monitor.
- Once Phase 1 (pdrum-derived engine, correctly adapted to ESP32-S3: block adapter,
  DC-offset, padType/choke fixes) and Phase 2 (peak-multiple re-strike check instead
  of a flat mask) are each confirmed solid on real hardware — use per-pad-type
  recorded waveforms (via the planned bounded-capture mechanism / app-side ADC Scope
  redesign, see the debug-logging-system discussion) as the evidence base for tuning
  mask/re-strike parameters *per pad type*, rather than one global guess.
- Deliberately sequenced last: a data-driven refinement on top of a baseline we
  already trust, not layered onto a system still being debugged.

**Immediate — Velocity floor / noise gate (next task, added 2026-07-04):**
- Even with real pads plugged into all four jacks, low-velocity spam still gets
  through on some inputs (observed via telnet + MidiView: near-continuous
  Note on/off + `05 03` SysEx at velocity 1 across multiple inputs).
- Distinct from `threshold` (which sets the *scan trigger* point that starts a
  hit capture) — this is a **post-detection floor**: discard a detected hit if
  its resulting raw ADC / MIDI velocity falls below a configurable floor, so
  marginal noise that still crosses `threshold` doesn't produce output.
- Goal: a software-tunable way to clean up hardware anomalies (floating jacks,
  cable/EMI noise, per-pad quirks) per input, so the rest of the system
  (DSP tuning, app UI, MIDI mapping) can be worked on without noise spam —
  works alongside `InputConfig.enabled` (which fully disables an input) rather
  than replacing it.
- Needs: new configurable field (per input, e.g. `velocityFloor`), applied in
  `main_esp32s3.cpp`'s hit-output path (`rawToMidi()` / the `hasHit()`/`hasHitRim()`
  blocks) before `MIDI.sendNoteOn()` and the `05 03` SysEx are sent. App-side:
  expose in Config/MIDI tab and PAD_GET/PAD_SET SysEx.
- Not yet designed in detail — first task next session is to work out where in
  the pipeline the floor should sit and whether it should be a single value or
  per-zone (head/rim) like `threshold` already is.

**Immediate — PDrum2Trigger sensing rewrite (top priority):**
- Implement PDrum2Trigger using Step 0 design doc (docs/sensing_rewrite_step0.md)
- Band-pass IIR → power domain (squaring) → decay-model retrigger mask
- DMA continuous sampling at 8kHz/channel replacing analogRead()
- Three sensing code paths: DUAL_PIEZO, PIEZO_SWITCH_CHOKE, SINGLE_PIEZO
- Drop-in replacement via TriggerEngine* — no main_esp32s3.cpp changes needed

**Firmware / hardware (priority order):**
1. Retune all DSP params for ESP32-S3 ADC noise floor (all pads)
2. Hi-hat firmware — GPIO1 analog read, CC output, open/close thresholds
3. Watchdog timer — ESP32-S3 hardware watchdog
4. Hard hit runaway — add loopTimes safety limit to sensing()
5. 22nF caps on ADC front-end — next PCB spin

**Satellite hardware (next PCB order):**
- ESP32-S3 satellite PCB (THT prototype designed, ready to order from PCBWay)
- 2× Neutrik NMJ6HFD2 jacks, 220k/220k battery voltage divider + 100nF cap,
  RGB LED, I2C breakout pads, 400mAh LiPo
- Wake-capable GPIO routing (EXT0/EXT1 or ULP) is a PCB design constraint
- SMD version to follow once THT prototype validated

**App (priority order):**
1. curves.py — shared curve math (VelocityCurveWidget + emulator)
2. IBM Plex font bundling
3. Interface mode preference — replace --dev flag with persistent QSettings
4. Hi-hat controller UI
5. Scope window: fix Ctrl+C copy, MIDI transport warning

**Stage 2 firmware (after PDrum2Trigger):**
1. ESP-NOW transport layer (head unit central, satellites peripheral)
2. Broadcast-based pairing handshake + LittleFS MAC storage
3. Satellite sleep model: Active → Standby (~5min) → Deep sleep (~15min)
   Coordinated by head unit, not per-unit independent
4. RGB LED status on satellites

**Design / brand:**
- BOAL colour palette and identity exploration
- Fusion 360 case design for BT-1 family
- ClickBox hardware planning (nRF52840, eInk + frontlight, NeoPixel)

---

## Repo

github.com/punky-labs/edrum-project

## Dev Environment

- Windows: VS Code + PlatformIO (pioarduino platform) + Claude Code CLI
- MCP filesystem server: D:\Dev\eDrum\edrum-project\ mounted for Claude Desktop
- upload_protocol = esptool, upload_port = COM9 (XIAO ESP32-S3)
- board_build.filesystem_size = 0.5m in platformio.ini
- Platform pinned: pioarduino 53.03.11 (not 'stable' — causes cache mismatch)
- Build number: firmware/version.txt (integer) +
  firmware/scripts/increment_build.py (PlatformIO extra_scripts pre:)
- Primary build env: [env:xiao_esp32s3_head] (COM13). RP2040 env [env:xiao_rp2040]
  RETIRED (commented out) — the new `adc_continuous` DMA sampler is ESP32-S3-specific
  and the RP2040 + MCP3008 path (~2 kHz, no continuous DMA) cannot run the engine.