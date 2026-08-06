# eDrum Project State
Last updated: 2026-07-25 — Hi-Hat Pedal CC v1 IMPLEMENTED AND
HARDWARE-VALIDATED: real pedal confirmed working end-to-end against
Addictive Drums 2 (openness CC responding correctly; needs further tuning,
not a bug). See "Hi-Hat Pedal CC v1" section below for full detail,
including a genuine ADC-sampler heap-fragmentation bug found and fixed
along the way. Next task: app UI wiring for the hi-hat controller panel.
Prior milestone still stands and is unchanged by this session: App UI
wiring for the Secondary Trigger Behaviours v1 + Scan v3 fields is
IMPLEMENTED and self-verified via a headless PyQt6 instantiation test (see
"App UI Wiring" section below) — still NOT validated on real hardware/
emulator, that remains open. Project is in REAL-WORLD PLAYING TESTING mode.

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
- GPIO1 → hi-hat controller (A0) — pedal FSR, sampled as the 9th ADC1
  channel alongside the 4 pad jacks; openness → MIDI CC IMPLEMENTED AND
  HARDWARE-VALIDATED 2026-07-25 (see "Hi-Hat Pedal CC v1" section below)
- ADC front-end: 1kΩ series resistors + BAT85 clamp diodes + 1MΩ pull-down
  (22nF caps not yet fitted on interim board — target for next PCB spin)
- 4 stereo TRS jacks → 8 ADC channels (4 jacks, dual-zone capable)
  - **Tip = rim/switch channel; Ring = head/piezo channel** (corrected 2026-07-06 —
    see "Hard-won" note below; was previously documented backwards)
- 1 mono jack → GPIO1 directly (hi-hat controller, jack 4 — CC output
  IMPLEMENTED AND HARDWARE-VALIDATED 2026-07-25; no chick/pedal-close note
  yet, deliberately deferred)
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

- SysEx v0.3, manufacturer ID 00 7D
- Spec: docs/sysex_spec.md (authoritative)
- NUM_INPUTS = 5 (4 jacks + 1 hi-hat)
- INPUT_ID range: 00–04
- Link/unlink/input-status commands (`02 08`/`02 09`/`02 0A`) are
  IMPLEMENTED and in active use (`pad_config_tab.py`'s refresh worker calls
  `02 0A` on every input load) — an earlier version of this doc incorrectly
  said they'd been removed; corrected 2026-07-14 after checking `SysEx.cpp`
  directly rather than trusting the old note.
- 108 Python self-tests passing (expanded 2026-07-14 for the Secondary
  Trigger Behaviours v1 + Scan v3 SysEx extension — see that section above)

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
- Jack 4: note=44 (hi-hat pedal CC — CC output IMPLEMENTED
  2026-07-25 via ccNumber=4/ccChannel=10, not the note field; see
  "Hi-Hat Pedal CC v1" section)

---

## Pad Type Architecture (settled design — drives firmware + app)

Three distinct pad types identified from waveform analysis across all
tested pads. Each requires structurally different sensing logic.

### DUAL_PIEZO (value: 0)
Pads: Roland PDX-8, PDX-12
- Head: piezo (tip channel), Rim: piezo (ring channel)
- Both zones produce velocity-sensitive analog signals
- **SUPERSEDED 2026-07-12 — see "Secondary Trigger Behaviors v1" section below.**
  The ratio-based, mutually-exclusive discrimination described below was
  grounded in the old piezo/switch pad mental model and is being replaced by
  independent, layered head+rim detection (a real snare rimshot is both
  sounding together, not one-or-the-other). Kept here as historical record.
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

## Secondary Trigger Behaviors v1 — Snare Rim + Cymbal Choke (2026-07-12)

**Status: IMPLEMENTED 2026-07-12; DUAL_PIEZO classification PARTIALLY REVERTED
2026-07-13, then cross-stick REDEFINED 2026-07-13 (see below). Snare rim
(ratio classification + redefined cross-stick) CONFIRMED WORKING on real
hardware 2026-07-13 — all real rimshots and all real cross-stick attempts
classified correctly across multiple sweeps. Cymbal choke/alternate-note
implemented but not yet exercised on hardware to the same depth.**

**Cross-stick redefinition CONFIRMED WORKING (2026-07-13):** after the
redefinition below, a full batch of genuine "stick across rim" attempts
(the same technique that previously failed under the old head-presence rule)
all correctly produced `evt=xstick`. **`crossStickCutoff` calibrated by feel
to 100** (well above the 25 placeholder — real cross-stick technique on this
pad produces a wider velocity range than expected). **New known limitation,
deliberately parked:** the cutoff is a hard boundary, so the transition from
cross-stick to rimshot feels harsh — `vel=99` plays the cross-stick note,
`vel=101` plays a completely different rimshot note/timbre, no blending.
Same category as other binary-decision trade-offs already accepted this
project (quieter-second-hit, clipped-vs-clipped) — a crossfade/blend zone
would be the natural fix, deliberately deferred in favour of finishing
cymbal choke first.

**Cross-stick redefinition (2026-07-13) — stop detecting the physical technique;
cross-stick = "rim won classification AND its curved velocity is soft".** The
previous rule (rim fired AND head stayed below its own threshold) failed on this pad:
genuine "stick across the rim, zero head contact" attempts still produced headpk
519–683 (overlapping a genuine soft head hit at headpk 529) and ~89–109% ratio
(near-identical to genuine hard rimshots at 95–106%) — this pad's head/rim mechanical
coupling defeats BOTH head-presence and ratio as cross-stick discriminators. So
classification (STAGE 1: ratio when both fire, or default-to-rim when only rim fired)
now only decides whether **rim wins at all**; a SEPARATE STAGE 2 check, applied after
rim has won, compares the CURVED rim velocity against a new `crossStickCutoff` — soft →
cross-stick note, hard → normal rim note. New field `crossStickCutoff` is in **MIDI
velocity units (0–127), NOT raw ADC** (deliberately different from every other
threshold/margin in this project — flagged in-comment so it isn't "corrected" later);
telnet `w <input> xstickcut <v>`; placeholder default **25** (Andrew's test value,
unvalidated). `rimThreshold` still the prerequisite floor gate for "rim fired",
untouched. Adds one InputConfig field → **uploadfs / config reset required.**

**PART A revision (2026-07-13) — reverted from layering back to RATIO classification,
based on real hardware data.** First hardware captures on the PDX-8 showed heavy
head↔rim cross-channel bleed: **confirmed head hits read 19–33% rim/head, confirmed
rim hits read 153–173%.** That means on essentially ANY strike BOTH zones clear their
own absolute thresholds, so the "independent layering" model fired a spurious second
note nearly every hit — ratio classification is the genuinely better fit for this
pad, not merely the simpler one. What changed and what was kept:
- CHANGED: when BOTH `headFired` && `rimFired` (the ambiguous case), classification
  is now mutually-exclusive by ratio — `rimBest*100/headBest > rimRatioThreshold`
  → rim, else head. Only ONE note fires. `hit`/`hitRim` are no longer both set for a
  plain dual strike; the two independent `if`s in main still stand (needed for the
  cross-stick vs rim-slot split) but only one of head/rim fires per dual hit now.
- KEPT (not reverted): the either-channel Scan-start gate; rim's independent velocity
  SCALING via `rimThreshold`/`rimSensitivity`/`rimCurve` (only the classify decision
  reverted, not the scaling); cross-stick exactly as-is (`rimFired && !headFired`).
- REFINEMENT over the pre-v1 ratio design: `rimThreshold` is now a real prerequisite
  gate — rim must clear its own absolute floor before the ratio is even considered
  (cheap guard against tiny rim bleed tipping the decision when head is very quiet).
- `rimRatioThreshold` default 40 → **70** (centered in the 33%↔154% real gap; only 4
  data points, a reasoned placeholder — a soft→hard sweep on both zones is a future
  task). Old `firstPeakChannel` tiebreaker dropped (superseded by the rim gate + data
  threshold). New `ratio=` diagnostic on `[HIT]`/`[RIM]` (real value only in the
  both-fired case, else -1) to keep tuning from data.

**Original PART A (2026-07-12, now superseded above):**
- PART A (DUAL_PIEZO): Scan now starts on EITHER channel crossing its own threshold
  (the architectural correction); ratio discrimination replaced by independent
  layered head+rim detection + cross-stick (rim-fired, head-below-threshold); rim
  scales through its OWN `rimThreshold`/`rimSensitivity`/`rimCurve`; `crossStickNote`
  added; `rimRatioThreshold` left in place but no longer read. `hit`/`hitRim` can now
  BOTH be true in one block (layering) — main's old mutually-exclusive if/else split
  into two independent `if`s; new `hasHitCrossStick()` signal.
- PART B (PIEZO_SWITCH_CHOKE): existing choke logic UNCHANGED; new concurrent
  alternate-note (`alternateNote`, gated by instantaneous switch>chokeThreshold AND
  headBest>`minAltNoteVelocity`) via new `hasHitAlt()` signal; `kChokeHoldMs` constant
  is now the tunable per-input `chokeHoldMs` field.
- 7 new telnet-`w`-only InputConfig fields (NOT in SysEx): rimthresh, rimsens,
  rimcurve, xstick, altnote, minaltvel, chokehold — shown in `s` dump. `[HIT]`/`[RIM]`
  prints gained `evt=` (head/altnote/rim/xstick) to distinguish all paths.
- InputConfig grew again → a LittleFS `uploadfs` / config reset is REQUIRED before
  testing (new fields load at defaults). Original design notes below.

### Context: rim/switch behaviour is per-pad-role, not per-padType

Reframing that unblocked this design: "rim" is not one feature. Real pad
roles need genuinely different rim/switch behaviours, and `padType` (which
sensor is on the rim channel — piezo vs switch) doesn't capture that on its
own. Full behaviour survey by pad role:

| Pad role | Rim channel is | What it should DO |
|---|---|---|
| Kick | absent | nothing |
| Tom | 2nd piezo, usually unused | nothing, or a separate instrument (deferred, v1 doesn't need it) |
| Cymbal | switch | choke (note-off/aftertouch), sometimes an alternate note |
| Ride bell | switch, on a SEPARATE jack | alternate note, borrowing the ride jack's velocity — **needs cross-jack coupling, deferred, see below** |
| Snare | 2nd piezo, genuinely used | rimshot (layered with head) and/or cross-stick |

**Deliberately deferred, not designed here:**
- **Ride bell (cross-jack coupling).** The only item in this whole survey
  that breaks a fundamental architecture assumption: today's system is one
  `TriggerEngine` per jack, fully independent, with zero inter-jack
  awareness. Needs its own design conversation, not a bolt-on.
- **Tom rim as an independent second instrument**, and **stereo jack split
  into two fully independent SINGLE_PIEZO pads** (same physical channels,
  different config only — architecturally cheap, but real config/UI work).
  Both explicitly out of scope for v1 per Andrew's own use-case notes.

### Snare rim — full spec

**Core reframe: a real snare rimshot is head AND rim sounding together, not
an either/or choice.** The old ratio-based mutual-exclusivity was inherited
from the switch-pad mental model (where rim genuinely can't co-occur with a
head velocity) and doesn't reflect how a real snare rimshot works physically.

**Design (agreed in full):**
- **Head:** unchanged — own threshold, own sensitivity, own curve, own note.
- **Rim:** INDEPENDENT of head — own threshold, own sensitivity/gain, own
  curve. Rim velocity is no longer computed through the head's `curve()`
  call (`curve(velocityRim, headThreshold, headSensitivity, curvetype)` —
  this was flagged as a real existing defect during design, not just a
  missing feature: head and rim piezos see genuinely different mechanical
  energy for the same strike, so sharing one scale can only ever be
  correctly calibrated for one of the two channels).
- **Layering:** both head and rim can fire from a single physical strike,
  each sending its own independent MIDI note. No mutual exclusivity.
- **Cross-stick (exception to layering):** if rim clears its own threshold
  AND head stays below its own threshold → suppress the head note (moot,
  since head didn't clear its threshold anyway) and send a dedicated
  cross-stick note INSTEAD OF the rim note. **Deliberately kept simple for
  v1** (2026-07-12): a three-way split (distinguishing a true butt-on-head
  cross-stick from a pure rim-only edge click via a second, lower
  `headFloor` threshold) was considered and explicitly rejected as
  over-engineering ahead of real data — the TD-3 manual's own recommended
  cross-stick technique (stick laid across the rim, not butt-on-head)
  naturally keeps head signal low without needing a special detection
  algorithm to compensate for a technique nobody's actually using. Test the
  simple 2-way rule for real; only add a 3-way split if it turns out to
  matter in practice.
- **ARCHITECTURAL CORRECTION (2026-07-12), required for cross-stick to be
  detectable at all, not optional complexity:** `Scan` currently only starts
  when HEAD crosses `headThreshold` (`if (piezoValue > headThreshold)
  startScan(...)` in `IDLE`) — rim is tracked passively only once a
  head-initiated scan is already underway. Under this gate, head's recorded
  peak at scan-end is *guaranteed* to already be above `headThreshold` (it's
  what started the scan), so "head stays below threshold" can structurally
  never be true — cross-stick could never fire. **Fix: `IDLE` must start a
  scan when EITHER channel crosses its own threshold** (head > headThreshold
  OR rim > rimThreshold), with both channels tracked throughout regardless
  of which one triggered it. This matches the physical reality: a genuine
  cross-stick may never produce meaningful head signal at all.
- **Net result: three possible outcomes per strike** — head-only, rim(+head)
  layered, or cross-stick (exclusive, replaces what would have been the rim
  note). Not a simple two-way split.

**Design alternatives considered and explicitly rejected, recorded so they
aren't re-litigated later:**
- **Ratio-based cross-stick discriminator** (rim high relative to head, not
  just rim-high-head-low in absolute terms) — correctly identified as more
  robust against a hypothetical "soft rimshot" edge case, but rejected as
  solving a case that doesn't really exist musically: a rimshot is played
  specifically for its sharp/loud character, so a deliberately soft rimshot
  isn't really a real playing target — it would sound close to a cross-stick
  anyway. Simple absolute-threshold version matches majority real drum-module
  precedent (confirmed against TD-3's own manual wording) and is
  deliberately chosen over the more theoretically robust ratio version, per
  this project's "solid and simple first, add nuance once proven" discipline.
- **TD-3's own "rim as fully independent secondary instrument" model**,
  considered as a wholesale replacement for our discrimination approach, was
  rejected as a *universal* fix — it's exactly right for snare, but wrong or
  unwanted for every other pad role in the survey table above. This is why
  the fix ended up being "add a configurable rim-behaviour concept" rather
  than "adopt TD-3's mechanism wholesale."
- **DWe's separate "rim" vs "rimshot" sounds** (a further distinction this
  project's config app was seen to support) is a real, known further
  refinement — explicitly deferred, not designed now. Note: since the final
  cross-stick design uses absolute thresholds rather than a ratio input, this
  is NOT automatically free later the way it would have been under the
  rejected ratio-based approach — it would need its own design pass if
  picked up.

**New config needed (not yet built):**
- Rim: independent `rimThreshold`, `rimSensitivity`/gain, `rimCurve`
  (replaces sharing head's threshold/sensitivity/curve)
- `crossStickNote` (separate from the existing rim note)
- The existing `rimRatioThreshold` field becomes unused for this mode —
  decide whether to retire it or repurpose it (matching this project's
  established repurposing pattern for genuinely-dead fields) once the new
  fields are known to need config-field slots.

### Cymbal choke — LOCKED SPEC (2026-07-12; choke OUTPUT MECHANISM CORRECTED
2026-07-14, ready to implement)

**Choke: unchanged, existing detection mechanism.** Sustained-above-
`chokeThreshold` detection on the switch channel (`chokeHoldMs`, real-world
calibrated to ~500ms — see below), independent of head piezo scanning.
The existing one-shot `chokeDetected` latch/timing is COMPLETELY UNCHANGED
by this correction — only what gets SENT when it fires has changed.

**CORRECTED 2026-07-14: choke output is Polyphonic Aftertouch, NOT
Note-Off.** The 2026-07-12 "note-off is correct" conclusion was WRONG —
corrected after real evidence contradicted it (Andrew's actual Addictive
Drums 2 setup uses a dedicated choke MIDI note, not note-off on the head
note) and confirmed via targeted research: **"almost every brand uses
exclusively polyAT for cymbal choke" — Roland, Yamaha, 2box, ATV, EFnote,
Alesis all use Polyphonic Aftertouch, value 127 on grab / 0 on release,
sent on the SAME note number(s) the cymbal is already sounding on** (not a
separate dedicated choke note). Worth remembering for next time: the
original note-off research checked plausible *drum-engine* conventions in
the abstract but didn't check real e-drum *hardware* convention specifically
— the actual installed base of Roland/Yamaha/2box/etc. modules all agree on
aftertouch, which is a much stronger, more specific signal than generic
"how do drum engines implement choke groups."

**Real-world constraint that shaped the final design: chokes can come many
seconds after the original strike** (Andrew: cymbal hits can sustain
multiple seconds; a choke to cut that sustain may come 3-4+ seconds later,
routinely, not as a rare edge case). This ruled out an initial "hold the
Note-On open, send Note-Off only once choked or on a timeout" design that
was drafted and then correctly rejected: **note duration doesn't need to
match sample duration** — a drum VST's internal voice engine tracks its own
sounding voices independently of raw Note-On/Note-Off timing (that's how
one-shot drum samples work generally), so a quick Note-On/Note-Off exactly
as already implemented, with aftertouch arriving completely independently
and arbitrarily later, works correctly for live playing. (The one MIDI
source that suggested holding notes open longer via "Gate Time" was solving
a DAW *recording/timeline* problem — some plugins/DAWs can't store an
aftertouch event against a note already closed in *recorded track data* —
not a live-playing problem; not applicable here.)

**Final mechanism, deliberately simplified from an initial 127-then-later-0
stateful design down to a single instantaneous pulse:**
- On the EXISTING `chokeDetected` firing (unchanged sustained-hold
  confirmation), send, for BOTH the head note and `alternateNote` (both,
  unconditionally — either note may still be "ringing" in the receiving
  engine, choke should stop both): `PolyAftertouch(note, 127)` IMMEDIATELY
  followed by `PolyAftertouch(note, 0)`.
- **Deliberately NOT stateful** (no tracking of "currently gripped," no
  separate release-triggered event) — considered and rejected: the `127`
  value is what does the real work ("cut this voice now"); a *following*
  release event has no meaningful further job for a typical one-shot choke
  implementation, since choking is inherently one-directional (letting go
  doesn't un-choke a real cymbal either). Sending `127` immediately followed
  by `0` as ONE pulse is MORE robust than sending `127` alone, not a
  compromise — it satisfies either a level-triggered engine (reacts to the
  value) or an edge-triggered one (reacts to the 127→0 transition) without
  needing to know which convention the receiving software actually uses.
  Explicitly analogous to how Note-On/Note-Off already fire as one
  back-to-back pulse in this system rather than being held open — aftertouch
  is being repurposed the same way, an artifact of MIDI's keyboard/continuous-
  pressure origins being reused for a discrete drum-trigger event.
- Reuses the EXISTING `chokeDetected` latch and its exact hold-time timing
  unchanged — genuinely simpler to build than the earlier stateful draft,
  not just simpler conceptually. No new state, no timeout, no "does a new
  hit interrupt an open note" logic needed at all.

**Note-off is dropped entirely for choke** — the existing head-hit
Note-On/Note-Off (sent at strike time, unrelated to choke) is completely
unchanged; choke no longer sends its own Note-Off at all, only the
aftertouch pulse pair described above.

**[DONE 2026-07-14] Confirmed working on real hardware.** Verified via
library source inspection that `MIDI.sendAfterTouch(note, pressure, ch)`
(3-arg overload) genuinely sends Polyphonic (per-note) Aftertouch
(`AfterTouchPoly`, status `0xA0`) — not the 2-arg Channel Aftertouch
overload, which would have incorrectly affected every note on the channel.
Both head note and `alternateNote` confirmed receiving the 127→0 pulse pair
independently (verified by changing `altnote` live and confirming the
second pulse followed the new number). Andrew confirms it "works really
well" after live tuning — real settings changes TBD/to be recorded.

### Choke hold-time is fragile against real grip variation — root cause found
and fix designed (2026-07-14, not yet implemented)

**Symptom, from real hardware tuning:** `chokeHoldMs` had a suspiciously
narrow, fragile sweet spot — `5` fired too easily, `10` was hard to trigger
at all — nowhere near the ~500ms Andrew's own real-world domain knowledge
suggested (see the earlier chokeHoldMs calibration note above). Settled at
`7` as a working compromise, but investigated further rather than just
accepting it, since a 5ms-wide window between "too sensitive" and "too hard"
is itself a red flag, not a normal tuning characteristic.

**Root cause, confirmed via code inspection
(`PDrumTrigger.cpp`'s choke block):** the hold-time accumulator has ZERO
tolerance for interruption — `chokeAbove_` resets to false, and
`chokeAboveSince_` gets discarded, the INSTANT `rimValue` dips even one
sample below `chokeThreshold`. A real hand gripping a physical switch is
never perfectly, continuously above a fixed level (grip pressure shifts,
fingers move, genuine contact bounce) — so a LONGER `chokeHoldMs` doesn't
make detection more robust, it makes it MORE fragile, since a longer
required unbroken window gives natural grip variation more opportunity to
land a brief dip and reset the whole accumulation. This is why 500ms
(needing perfectly unbroken contact for half a second) is unreachable in
practice, and why the 5-10ms window itself was so narrow — both symptoms of
the same underlying gap, not independently-tunable behaviour.

**Fix designed: debounce the RELEASE, not the hold — a grace period that
tolerates brief dips without resetting the accumulator.** [DONE 2026-07-14,
IMPLEMENTED, NOT YET HARDWARE-VALIDATED] New parameter
`chokeReleaseGraceMs` (telnet-`w`-tunable, same pattern as everything else;
placeholder default 30ms, real calibration deferred same as every other
constant this project has flagged). Confirmed in code: `belowSince_` member
added, release-side debounce logic implemented exactly per this spec in
`PDrumTrigger.cpp`'s choke block. Mechanism: when the switch dips below
`chokeThreshold` while `chokeAbove_` is already true, don't reset
immediately — start a separate release-pending timer. Only treat it as a
genuine release (reset `chokeAbove_`) once the dip itself has persisted for
`chokeReleaseGraceMs`. Any real contact within the grace window cancels the
pending release and the original hold-time accumulation continues
uninterrupted, as if the dip never happened. NEXT: hardware test with
`chokeHoldMs` raised toward the realistic ~500ms target now that it should
be reachable, confirm a genuine sustained grab reliably triggers and a
brief incidental touch does not.

**Design alternatives considered and rejected in favour of this simpler
option:** a leaky/decaying confidence accumulator (adds a decay RATE to
tune instead of a duration — less consistent with every other duration-based
parameter in this system) and an N-of-last-M sliding-window majority vote
(needs a sample buffer, real added complexity). Release-side debounce solves
the actual problem with the same shape of mechanism (`w`-tunable millis()
duration) already used everywhere else in this codebase — matches this
project's recent pattern (cross-stick, choke output) of favouring the
simplest mechanism that solves the real problem over a more "thorough" one.

**Separate, smaller thing noticed while investigating (not the bug being
fixed, just worth knowing):** `chokeDetected` re-arms immediately after
firing (`chokeAbove_ = false`), so a long continuous grab re-fires choke
repeatedly, roughly once per `chokeHoldMs`, for as long as it's held — not
just once. Given choke now sends a discrete aftertouch pulse (not a
sustained signal), a long grab currently sends that pulse multiple times.
Likely harmless (each repeat just re-confirms "still choked" to the
receiving engine) but flagged here in case it ever causes an audible/visible
issue — not being fixed as part of this task, deliberately out of scope.

**Alternate note: NEW, independent, transient-triggered — runs concurrently
with choke, not a mode selection between the two.** [unchanged from
2026-07-12, see mechanism below]
BOTH behaviours available at once (confirmed against how commercial modules,
including basic ones like the original HelloDrum, handle this) — they're
distinguished by signal SHAPE (sustained vs. transient), not a per-pad
toggle a user picks between.

Mechanism: at the instant a head hit commits (Scan settles, past
`headThreshold` — the existing hit-confirmation moment), check whether the
switch channel is ALSO above `chokeThreshold` right then, AND head velocity
clears a new minimum-velocity field (distinguishes a genuine edge hit from
incidental contact as a hand comes down to grab the cymbal). If both true →
send `alternateNote` (using the head hit's own velocity) INSTEAD OF the
normal head note. No suppression of choke, no delay/wait-and-see — choke
keeps monitoring in its own fully independent lane regardless of what the
alternate-note check just did.

**Deliberate benefit of full independence, not just simplicity:** a hard
edge hit followed by a grab-to-mute plays correctly in sequence with zero
extra logic — alternate note fires immediately at the strike, choke fires
shortly after once the grip genuinely sustains past the hold time. Exactly
matches real playing intent.

**Edge case identified and deliberately deferred (not a gap in the spec, a
conscious scope decision):** a head hit landing while the switch is ALREADY
elevated from an existing, ongoing choke (not a fresh coincident peak) would
still pass the "switch is above threshold" check and could misfire as an
alternate note. A "freshness" check (recent `chokeAboveSince_`, not a
long-running grab) would close this, but was explicitly left out: striking a
cymbal at the same moment as choking it is already an unusual/incorrect
playing situation, not worth the added complexity for v1. Revisit only if it
turns out to matter in practice.

**New config fields needed (not yet built):**
- `alternateNote` (separate from the existing head note)
- Minimum head velocity for the alternate-note coincidence check (raw ADC
  domain, matching every other threshold/margin field in this system)
- `chokeThreshold` is SHARED/REUSED for both choke and the alternate-note
  check — no new threshold field, same signal interpreted on two timescales
  (sustained for choke, instantaneous-at-hit for alternate-note).

**Carried over from the original open-items list, still applies:**
- `kChokeHoldMs` (5ms) becoming a per-input `w`-tunable constant, same
  placeholder-now-calibrate-later treatment as `margin`/`capValue`/
  `scanMargin`. **[CALIBRATED 2026-07-12, from real playing]: 5ms is far too
  short — a proper choke hold is closer to ~500ms in real-world use.** The
  5ms default was carried over unchanged from the old hardcoded constant
  with no real-world basis; now implemented as `chokeHoldMs` (telnet `w`),
  set live via `w <input> chokehold 500` (or similar) rather than needing a
  reflash. Worth raising the compiled-in default itself next time the
  firmware is touched, so fresh installs don't start from the same trap.
- `chokeThreshold` real per-pad calibration — not a design question, a
  data-gathering task using the same evidence-based methodology as
  everything else in this project; the field and mechanism already exist.

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

## SysEx Extension — Secondary Trigger Behaviours v1 + Scan v3 (2026-07-14)

**Status: IMPLEMENTED AND SELF-TESTED 2026-07-14 (firmware + Python protocol
layer only — app UI wiring is the separate next task, not started).** This
is the prerequisite work identified by the App UI parity inventory pass (see
Pending section below for that history): the 12 fields added during the
Secondary Trigger Behaviours v1 + Scan v3 firmware phase were telnet-`w`-only
and had no SysEx commands, so no UI slider could round-trip them even once
built. This session designed and implemented the wire additions; UI widgets
themselves are untouched.

### What was built

**Firmware (`firmware/src/midi/SysEx.h` + `SysEx.cpp`):**
- 12 new individual `SET` commands, Category 02, bytes `0x11`–`0x1C` — one
  per field, matching the existing one-command-per-field pattern (not
  bundled), so live slider drags stay cheap single-field writes.
- One new bundled `GET`/`RESP` pair, `0x1D`/`0x1E`, mirroring the existing
  `02 06`/`02 07` pattern rather than requiring 12 separate round-trips on
  every input refresh.
- Preset export (`04 06`) grew from 24 to 43 bytes per input (append-only —
  first 24 bytes byte-for-byte unchanged) so presets round-trip the new
  fields too.
- **Real bug fixed along the way:** `chokeEnabled` (`0x10`) was already
  defined in both `SysEx.h` and `protocol/sysex.py`, and the app's existing
  "Choke" checkbox was already sending it — but `SysEx.cpp`'s `handlePad()`
  had no `case` for it, so every toggle of that checkbox silently fell
  through to the unknown-command ack. Added the missing handler.
- Encoding follows existing precedent exactly: single byte for anything
  ≤127 (`emaAlpha`, `rimCurve`, `crossStickNote`, `crossStickCutoff`,
  `alternateNote`), 14-bit split via the existing `encode14`/`decode14`
  helpers for raw-ADC/ms fields.

**Python (`app/protocol/sysex.py`):**
- Matching constants, 12 new builders, the bundled GET/RESP parser, and an
  updated preset-export parser for the 43-byte records.
- **Naming landmine found and fixed:** the file had two aliases —
  `build_set_rim_sensitivity = build_set_rim_ratio_threshold` and
  `build_set_rim_threshold = build_set_choke_threshold` — left over from
  when `02 0E`/`02 0F` were repurposed in June. Now that *real*
  `rimThreshold`/`rimSensitivity` fields exist as of this session, those
  alias names would have silently pointed at the wrong command if reused.
  Confirmed unused (`pad_config_tab.py` already calls the explicit
  `build_set_rim_ratio_threshold`/`build_set_choke_threshold` names) and
  deleted; new fields got unambiguous names instead (`build_set_rim_gate_
  threshold`, `build_set_rim_scale` — deliberately avoiding "threshold"/
  "sensitivity" alone, since those words are already claimed by the ratio/
  choke fields).
- Self-test suite expanded from the prior count to **108 assertions**,
  covering: all 12 new builders' cmd_low + payload round-trip, a regression
  guard that the new rim commands' byte values never collide with `0x0E`/
  `0x0F` and that the deleted aliases genuinely don't exist, the
  `crossStickCutoff` 0–127 MIDI-velocity range validation, the bundled
  GET/RESP round-trip, the `chokeEnabled` fix, and a full 43-byte preset
  export record round-trip. **Run and confirmed passing** (copied into a
  sandbox and executed directly — genuinely run, not just written) with
  zero regressions against every pre-existing test in the file.

**Docs (`docs/sysex_spec.md`):** updated to v0.3 — new `02 11`–`02 1E` rows,
`04 06`'s grown record documented, and the `02 0E`/`02 0F` rows corrected to
state their actual repurposed meaning (the doc previously still called them
"rim sensitivity"/"rim threshold" despite `SysEx.cpp` having repurposed them
months earlier — a real doc-vs-code drift caught during this pass, not just
a gap).

### Command layout reference

| Byte | Field | Encoding |
|---|---|---|
| `0x10` | `chokeEnabled` (handler was missing — now fixed) | 1 byte bool |
| `0x11` | `scanMargin` (ALL pad types) | 14-bit |
| `0x12` | `settleWaitMs` (ALL pad types) | 14-bit |
| `0x13` | `emaAlpha` (ALL pad types) | 1 byte (0–100) |
| `0x14` | `rimThreshold` (DUAL_PIEZO) | 14-bit |
| `0x15` | `rimSensitivity` (DUAL_PIEZO) | 14-bit |
| `0x16` | `rimCurve` (DUAL_PIEZO) | 1 byte |
| `0x17` | `crossStickNote` (DUAL_PIEZO) | 1 byte |
| `0x18` | `crossStickCutoff` (DUAL_PIEZO) — MIDI velocity 0–127, NOT raw ADC | 1 byte |
| `0x19` | `alternateNote` (PIEZO_SWITCH_CHOKE) | 1 byte |
| `0x1A` | `minAltNoteVelocity` (PIEZO_SWITCH_CHOKE) | 14-bit |
| `0x1B` | `chokeHoldMs` (PIEZO_SWITCH_CHOKE) | 14-bit |
| `0x1C` | `chokeReleaseGraceMs` (PIEZO_SWITCH_CHOKE) | 14-bit |
| `0x1D`/`0x1E` | bundled GET/RESP for all 12 fields above | — |

### Other findings surfaced during this pass, deliberately not fixed now

- **`presets_tab.py`** (the dev "Presets Editor" floating window, a
  *different* preset system from `pad_config_tab.py`'s "My Presets"
  dropdown — both go through `ui/presets.py`'s local JSON file, but with
  inconsistent field names) still reads/writes local preset dicts under the
  keys `rim_threshold`/`rim_sensitivity`, while `pad_config_tab.py`'s own
  preset-apply code reads `rim_ratio_threshold`/`choke_threshold` for the
  same conceptual slot. This mismatch predates this session and is
  independent of the SysEx work above (it's local-JSON-schema drift, not a
  wire issue) — flagged for whoever next touches the Presets Editor, not
  fixed here.
- **`project_state.md`'s own Protocol section** (below) previously stated
  "Link/unlink/input-status commands removed (02 08, 02 09, 02 0A)" — false;
  confirmed via `SysEx.cpp` that all three are implemented and in active use
  (`pad_config_tab.py`'s refresh worker calls `02 0A` on every input load).
  Corrected below.

### Open items — next task

- **App UI wiring is now DONE** — see "App UI Wiring — Secondary Trigger
  Behaviours v1" section immediately below for full detail.

---

## App UI Wiring — Secondary Trigger Behaviours v1 (2026-07-14)

**Status: IMPLEMENTED and self-verified via headless PyQt6 instantiation
test. NOT YET validated on real hardware or the emulator — that's the
immediate next step, and the actual visual layout (panel width/crowding)
hasn't been eyeballed on a real screen.**

### Scope agreed before building (design decisions, not defaults picked
unilaterally)

Of the 12 fields the SysEx Extension exposed, three were deliberately cut
from UI scope this round, and one got a simplified treatment:
- **`rimCurve`** — parked. Kept as an implicit linear curve for now; Andrew's
  call is that a real curve-type selector for this field is a bigger design
  exercise belonging in the Velocity Curve graph panel, not a bolt-on combo
  here. The SysEx command (`02 16`) and Python builder
  (`build_set_rim_curve`) still exist and work — just nothing in the UI
  calls them yet.
- **`scanMargin`, `settleWaitMs`, `emaAlpha`** — kept OUT of the UI
  entirely, per Andrew: these are system/algorithm-tuning parameters, not
  musician-facing pad behaviour, and don't belong in this app's UI at all
  for now. Also fully wired at the SysEx layer (`02 11`/`02 12`/`02 13`) and
  reachable via telnet `w` if ever needed — just no widget.
- **`crossStickCutoff`** — built as a plain slider in Trigger Settings, no
  special range labelling or visual treatment, per Andrew ("a slider is
  probably visual enough indication"). Its 0–127 MIDI-velocity domain (vs.
  raw-ADC for its slider neighbours) is left unmarked for now; friendlier
  range labelling is a deferred, later task once user-facing ranges are
  designed generally (see the parked 1–16-abstraction discussion earlier
  this session).

That left **8 fields** actually built this round.

### What was built

**Trigger Settings panel (`app/ui/pad_config_tab.py`) — 6 new sliders,**
following the exact existing pattern (`_TRIGGER_BUILDERS` entry + `params`
list entry + `_update_zone_visibility` group), same mechanism applied 6
times, not 6 separate designs:
- DUAL_PIEZO-only: `rimThreshold`, `rimSensitivity` (both raw ADC, 0–1023 —
  matches the existing `_sens` slider's range convention), `crossStickCutoff`
  (0–127, MIDI velocity)
- PIEZO_SWITCH_CHOKE-only: `minAltNoteVelocity` (raw ADC, 0–1023),
  `chokeHoldMs` (0–1000ms, headroom above the ~500ms real-world target),
  `chokeReleaseGraceMs` (0–200ms, headroom above the 30ms placeholder)

**MIDI tab — 2 new note-combo rows**, confirmed against firmware
(`main_esp32s3.cpp`) that neither needs its own channel field:
- `crossStickNote` (DUAL_PIEZO-only) — fires on the existing Rim Channel
  spinbox (firmware: uses `zone2MidiChannel`)
- `alternateNote` (PIEZO_SWITCH_CHOKE-only) — fires on the existing Head
  Channel spinbox (firmware: uses `midiChannel`)

**Refresh worker (`_RefreshWorker._fetch_input`) — 4th fetch step added.**
A real gap caught before it shipped: the worker only fetched status/pad-
config/MIDI-mapping (3 SysEx round-trips per input); without a 4th step
calling the new bundled `02 1D` GET, none of the new sliders/combos would
ever show real device values on refresh — only their populate-time fallback
defaults. Added, matching the existing 3 steps' structure exactly.

**Visibility grouping**: `_rim_midi_widgets` gained the cross-stick row;
a new `_choke_midi_widgets` group was added for the alternate-note row
(the MIDI tab previously only had dual/hi-hat groups, no choke-only group).

### Verification performed (this session, before declaring done)

No real hardware or emulator available in this environment, so verification
was done in layers, each one designed to catch a different class of bug:
1. **Syntax check** (`ast.parse`) — clean.
2. **Structural check** — confirmed every `_TRIGGER_BUILDERS` key has
   exactly one matching entry in the Trigger Settings `params` list and
   vice versa (no orphaned slider config, no unwired slider).
3. **Real headless PyQt6 instantiation** (`QT_QPA_PLATFORM=offscreen`) —
   built the actual `PadConfigTab` widget tree using the real, already-
   tested `protocol/sysex.py` (only non-logic dependencies like `theme.py`/
   `asset_loader.py` were stubbed). Confirmed the whole tree constructs
   without exception.
4. **Populated from fake device data** for both a DUAL_PIEZO and a
   PIEZO_SWITCH_CHOKE config, covering all 8 new fields — confirmed every
   new slider and both new note-combos land on the exact expected value.
5. **Visibility toggling verified across all 3 real pad types**, for both
   the Trigger Settings panel and the MIDI tab rows. First attempt showed
   false negatives across the board (including on `_rim_ratio`, pre-existing
   untouched code) — traced to a test-harness gap, not a real bug: the
   detail page sits on a `QStackedWidget` page and the MIDI rows sit on a
   non-default `QTabWidget` tab, and Qt's `isVisible()` only reflects reality
   once those parent pages are actually current. Fixed the test (drove it
   through the real `_select_input()`/tab-switch path instead of poking
   internals directly) and reran — all checks passed cleanly after that.
6. Slider-change and MIDI-change handlers exercised directly with no live
   transport connected — confirmed they no-op safely rather than raising.

**Explicitly NOT verified** (needs Andrew, hardware or emulator, real eyes):
- Live SysEx round-trip against real firmware (send/ack/refresh cycle)
- Actual visual layout — the Trigger Settings panel grew from 7 to 13
  possible columns (max 9 shown at once for any single pad type, since
  DUAL_PIEZO/CHOKE-specific groups are mutually exclusive) — whether that
  reads well on a real screen hasn’t been checked
- Preset apply/save round-trip for the new fields (deliberately NOT wired
  into `_on_preset_apply`/`_on_preset_save` this round — those still only
  cover the original 7 fields; extending the local "My Presets" schema to
  the new fields wasn't in scope and would compound the already-flagged
  `presets_tab.py` key-name mismatch if done carelessly)

### Next steps

- Andrew: connect real hardware or launch the emulator, confirm the new
  sliders/combos round-trip correctly and the panel reads well visually.
- Deferred, not urgent: extend local preset save/apply to the 8 new fields;
  resolve the `presets_tab.py` key-name mismatch (flagged in the SysEx
  Extension section above); revisit `rimCurve` alongside the Velocity Curve
  panel; the 1–16 abstraction layer discussed earlier this session.

---

## Hi-Hat Pedal CC v1 (2026-07-25)

**Status: IMPLEMENTED AND HARDWARE-VALIDATED. Real pedal + real hi-hats
confirmed working end-to-end against Addictive Drums 2 — openness CC
responding correctly. Andrew: "needs tweaking, but that's fine" (expected
tuning work, not a bug) — the core CC-output pipeline is proven.**

### What was built

**9th ADC channel (GPIO1, hi-hat FSR) added to the existing sampling
pipeline:**
- `AdcSampler::kMaxChannels` 8 → 9; `kChannelGpios` in `main_esp32s3.cpp`
  grew to `{1,2,3,4,5,6,7,8,9}` (GPIO1 prepended) — every existing pad's
  `streamCh()` lookup stayed transparently correct since none of them ever
  hardcoded a raw stream index, only GPIO numbers translated via the helper.
- New `firmware/src/sensing/hihat/HiHat.h/.cpp` — a small standalone class,
  deliberately NOT a `TriggerEngine`: the hi-hat is a continuous position
  sensor (FSR), not a transient/hit detector, so it doesn't share the pad
  engines' threshold/scan/mask model.
- Signal path: raw ADC → EMA smooth (α=0.15, heavier than the pad EMA
  default of 0.5 since this is a slow signal with no fast attack to
  preserve) → linear map `[0, 3400] → [0, 127]` (both bounds hardcoded from
  REAL measured data — see below, not guessed) → 7-step quantize,
  replicating HelloDrum's (`RyoKosaka/HelloDrum-arduino-Library`)
  `FSRSensing()` table exactly: steps at 0/20/40/60/80/100/127. A CC is sent
  only when the quantized step changes — this quantization ALONE is what
  prevents MIDI flooding; no separate hysteresis/debounce timer needed on
  top of it.
- Wired into `main_esp32s3.cpp`'s `loop()` as an independent code path
  AFTER the pad `for` loop (input 4 has no `TriggerEngine` —
  `kHeadCh[4]`/`kRimCh[4]` stay `-1`, deliberately untouched), gated by
  `g_inputs[4].enabled` and the existing `g_diagMode` early-return (same
  convention as the pad loop).
- MIDI output reuses `g_inputs[4]`'s EXISTING `ccNumber`/`ccChannel` fields
  — `Config.cpp` already defaulted these to CC4 (Foot Controller) / channel
  10 for every input, unused until this task. No new `InputConfig` fields,
  no SysEx/LittleFS changes needed.
- `platformio.ini`'s `[env:xiao_esp32s3_head]` uses an explicit source
  allowlist (`build_src_filter`), not globbing — `+<sensing/hihat/HiHat.cpp>`
  had to be added there or the new file wouldn't link. Worth remembering for
  any future new `.cpp` file in this env.

### Real calibration data (measured via the telnet `a` ADC dump, not guessed)

- Pedal up / resting: raw ADC ≈ 0
- Pedal fully pressed (hard): raw ADC ≈ 3400 (peaked 3439 in the capture)
- Polarity: pressure INCREASES the raw ADC value, which already matches the
  CC convention directly (CC 0 = open, CC 127 = closed, confirmed via DW
  eDrum / eDRUMin manual research) — no inversion needed anywhere in the
  pipeline.
- Press ramp took ~1.3s in testing — confirms this is a slow position
  signal, not a transient; no Scan-style fast-attack machinery is relevant
  here.

### A genuine bug found and fixed along the way: ADC sampler ESP_ERR_NO_MEM
(heap fragmentation)

Adding the 9th channel initially broke ADC sampling ENTIRELY (not just the
hi-hat — no pad hits registered either) — `AdcSampler::begin()` failed with
`ESP_ERR_NO_MEM` at the `adc_continuous_new_handle()` step. Root-caused via
new heap diagnostics (`heap_caps_get_largest_free_block(MALLOC_CAP_DMA)`):
`largest_dma_block` was 32756 bytes against a required 32768
(`kStoreBufBytes`) — 12 bytes short, PURE FRAGMENTATION (87KB+ free in
aggregate DMA-capable RAM, just no single contiguous block big enough), not
a real shortage. WiFi/TCP-IP stack bring-up fragments the heap with its own
internal allocations; the extra 9th-channel ring buffer growth (~16KB more
static internal RAM) was enough to tip the largest contiguous block below
the threshold.

**Fix: reordered `setup()` so `sampler.begin()` runs BEFORE
`devwifi.begin()`**, letting the ADC driver claim its one large contiguous
block while the heap is still pristine, rather than after WiFi's allocator
churn. `DevWiFi.h` explicitly documents WiFi/telnet as fully independent of
everything else, so this reorder carries no functional risk. Confirmed
fixed via the same heap diagnostic post-fix: `largest_dma_block` rose to
77812 bytes.

**Secondary fixes made during this investigation, worth keeping in mind for
future debugging:**
- `AdcSampler::begin()` now tracks and exposes the real `esp_err_t` + which
  of the three ESP-IDF calls (`new_handle`/`config`/`start`) failed
  (`lastError()`/`lastErrorStep()`), surfaced over both Serial and telnet —
  previously a `begin()` failure was a bare `false` with no diagnosable
  reason visible over telnet at all (serial RX is dead under USB MIDI, so
  this was a genuine blind spot, not just an inconvenience).
- `numChannels_`/`perChannelHz_` are now only set on FULL success (moved to
  just before `return true`), not speculatively at the top of `begin()` —
  previously a failed `begin()` still left `numChannels()` reporting the
  REQUESTED (not actual) channel count, which is what produced a misleading
  "configured...9 ch" boot line during a total sampler failure.
- The `sampler.begin(kChannelGpios, 8, 8000)` call had a real, separate bug
  at the moment GPIO1 was first added: the `numChannels` argument stayed
  hardcoded at `8` even after `kChannelGpios` grew to 9 entries, silently
  dropping GPIO9 (jack 3's head channel) from sampling. Fixed to `9` before
  the fragmentation issue was even found — two independent bugs stacked in
  the same short investigation, not one.

### Open items / next steps

- **App UI wiring for the hi-hat controller is the next task.** The right
  panel's Hi-Hat Controller page (see "App UI Architecture" section above)
  is currently still a placeholder. No SysEx/protocol work needed for the
  CC number/channel themselves (existing PAD_GET/SET already covers
  `ccNumber`/`ccChannel` generically for every input), but real UI design
  work is needed for what the panel actually shows/controls.
- Calibration bounds (`kAdcUp`/`kAdcDown` = 0/3400) and EMA alpha (0.15) are
  hardcoded constants in `HiHat.h`, NOT persisted/SysEx-exposed/
  telnet-tunable — deliberately deferred, same "placeholder now, calibrate
  properly later" treatment as other constants in this project. A proper
  calibration flow/UI is future work.
- Confirmed playable in real use against Addictive Drums 2; specific tuning
  targets (7-step granularity feel, EMA responsiveness, exact up/down
  bounds) not yet itemized — "needs tweaking" per Andrew, not yet broken
  down into concrete next actions.
- Chick/pedal-close note-on and splash detection remain explicitly OUT OF
  SCOPE (deferred from the original v1 scoping decision) — not started.

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

**MILESTONE 2026-07-14 — all basic firmware trigger functions now
implemented.** Snare rim, cross-stick, cymbal choke, and alternate-note all
confirmed working on real hardware (choke release-debounce grace period
implemented, awaiting hardware validation — see below). Project moves from
structured/synthetic testing to REAL-WORLD PLAYING as the primary testing
mode from here.

**Immediate, TOP PRIORITY — App UI parity (SysEx wiring DONE 2026-07-14,
UI widgets DONE 2026-07-14, pending Andrew's hardware/emulator validation):**
The config app's UI has not kept pace with the many new tunable fields
added during the firmware phase below — rim threshold/sensitivity, cross-
stick note/cutoff, alternate note/velocity, choke hold/release-grace. The
inventory pass, the SysEx wiring those fields needed, AND the actual UI
widgets are now all complete — see "SysEx Extension" and "App UI Wiring —
Secondary Trigger Behaviours v1" sections above for full detail. UI work
was self-verified via a headless PyQt6 instantiation test (widget tree
builds, populates from fake device data correctly, visibility toggles
correctly across all pad types) but **has NOT been run against real
hardware or the emulator, and the actual visual layout hasn't been
eyeballed on a real screen** — that's the immediate next step. Four
fields (`rimCurve`, `scanMargin`, `settleWaitMs`, `emaAlpha`) were
deliberately left out of UI scope per Andrew's call (rimCurve belongs with
a future Velocity Curve panel redesign; the other three are system-level,
not musician-facing) — fully wired at the SysEx layer regardless, just no
widget. UI sliders use raw ADC values matching firmware directly, per
Andrew's explicit call — the abstracted 1–16-style user-facing scale
discussed is a deliberately deferred, later, presentation-only pass.

**Immediate — choke hold-time release-debounce (2026-07-14, IMPLEMENTED,
NOT YET HARDWARE-VALIDATED):** `chokeReleaseGraceMs` fix confirmed present
in code (`belowSince_` member + release-side debounce logic in
`PDrumTrigger.cpp`'s choke block). Next real-world test: raise `chokeHoldMs`
toward the realistic ~500ms target now that it should be reachable, confirm
a genuine sustained grab reliably triggers and a brief incidental touch
does not. See "Choke hold-time is fragile..." section above for full detail.

**[DONE 2026-07-14] Secondary Trigger Behaviors v1 — Cymbal Choke.**
Aftertouch output (corrected from an earlier wrong note-off assumption,
confirmed via real Addictive Drums evidence + hardware-convention research)
and alternate-note mechanism both confirmed working on real hardware. See
"Cymbal choke — LOCKED SPEC" section above for full detail.

**[DONE 2026-07-13] Secondary Trigger Behaviors v1 — Snare Rim.** Ratio
classification (reverted from layering after real bleed data) + redefined
cross-stick (pure rim-velocity cutoff, not head-presence) both confirmed
working across multiple real hardware sweeps. `crossStickCutoff` tuning by
feel is ongoing (Andrew), not a code task. See "Secondary Trigger Behaviors
v1" section above for full detail.

**[DONE 2026-07-12] Scan Redesign v3 + EMA + Tunable Constants + the two
2026-07-08 retrigger-cancel revisions.** All implemented, hardware-validated
across 3 pads (PDX-8, CY-5, PD-7), no audible double-triggering or missed
hits. See "Scan Redesign + EMA Smoothing + Tunable Constants (v3)" section
above for full detail and remaining minor open items (scanMargin tuning,
margin/capValue calibration — none urgent).

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
2. [DONE 2026-07-25] Hi-hat firmware — GPIO1 ADC sampling, CC output,
   see "Hi-Hat Pedal CC v1" section above. Open/close thresholds (chick
   note) deliberately deferred, not started.
3. Watchdog timer — ESP32-S3 hardware watchdog
4. Hard hit runaway — add loopTimes safety limit to sensing()
5. 22nF caps on ADC front-end — next PCB spin

**Satellite hardware (next PCB order):**
- ESP32-S3 satellite PCB (THT prototype designed, ready to order from PCBWay)
- J8/J12 cross-board interconnect footprint mismatch (was `Conn_01x07` schematic
  symbol vs 4-position JST footprint, 3 signals unrouted) — CONFIRMED RESOLVED
  (2026-08-06, corrected by Andrew in KiCad). No longer a fab blocker.
- 2× Neutrik NMJ6HFD2 jacks, 220k/220k battery voltage divider + 100nF cap,
  RGB LED, I2C breakout pads, 400mAh LiPo
- Wake-capable GPIO routing (EXT0/EXT1 or ULP) is a PCB design constraint
- SMD version to follow once THT prototype validated

**Satellite hardware — next revision scope (added 2026-08-06, driven by the
in-shell wireless trigger direction — see "Custom drum triggers" future
direction above):**
- **All onboard jacks removed entirely, replaced with 3-pin JST per channel**
  (revised 2026-08-06 — supersedes the earlier same-day "nest JST inside the
  jack footprint" idea, which kept the jack footprint on-board for every
  channel; this is strictly better). 3-pin JST is a 1:1 match for the
  existing tip/ring/sleeve (head/rim/ground) wiring per dual-zone channel —
  not an approximation. Every board channel becomes a small JST pad only;
  boards get much more compact. 1/4" jacks become an external accessory:
  panel-mount jack + short pigtail terminating in a JST plug, used only where
  external jacks are actually wanted.
- **Panel-mount jack part TBD** — current BOM's Neutrik NMJ6HFD2 is a
  PCB-mount part; going fully off-board needs the panel-mount TRS jack from
  the same Neutrik family instead. Confirm exact part before next order.
- **Jack breakout pigtail — worth standardising as one reusable accessory**
  (panel jack + JST pigtail) shared across BT-1, BT-1 Expand, and satellites,
  rather than one-off wiring per unit — consistent with the shared
  boal_base.qss-style "design once, reuse across the product family"
  approach already used elsewhere. Not yet designed.
- **Hi-hat input uses the SAME 3-pin JST footprint for connector-family
  uniformity** (decided 2026-08-06), even though the hi-hat signal itself is
  mono (signal + ground, 2 conductors) — third pin simply left unconnected.
  Keeps one connector type across every channel/board rather than a special
  case for hi-hat.
- **USB-C charging access for in-shell units** — satellite board already has
  LiPo charge management circuitry; the XIAO's own USB-C port will be buried
  once mounted inside a shell. Needs a panel-mount USB-C jack wired via a
  captive pigtail routed out through the shell's air vent (same mechanical
  pattern as the jack-cable-through-airvent approach used for internally
  mounted trigger cabling), rather than relying on direct access to the
  board-mounted port. Panel-mount connector chosen over direct access for
  durability — repeated cable insertion stress lands on the panel mount, not
  the board-mounted port.
- Fold all of the above into the same PCB revision rather than doing
  separate spins.

**App (priority order):**
1. curves.py — shared curve math (VelocityCurveWidget + emulator)
2. IBM Plex font bundling
3. Interface mode preference — replace --dev flag with persistent QSettings
4. Hi-hat controller UI — firmware backend (CC output) now IMPLEMENTED
   AND HARDWARE-VALIDATED (2026-07-25, see "Hi-Hat Pedal CC v1" section
   above), so this is now unblocked and is the next task overall
5. Scope window: fix Ctrl+C copy, MIDI transport warning
6. **In-app help/reference page** (added 2026-07-14, menu-launched) —
   lists every field, what it does, and its range. Real content-authoring
   task, not a quick add — covers 12+ new Secondary Trigger Behaviours v1
   fields plus everything pre-existing. Open design question: static
   reference vs. context-sensitive to the selected pad type.
7. **Extract `GM_PERCUSSION` (currently hardcoded in `pad_config_tab.py`)
   into a standalone loaded file** (added 2026-07-14) — matches the
   load-from-file pattern `pad_names.py`/`presets.py` already use in this
   codebase; mechanically the simplest item in this backlog. Open design
   question before starting: file format — plain text (as literally
   asked for) vs. JSON like the other two loaders — worth deciding
   deliberately since it constrains item 8.
8. **Support loading a different/alternate instrument list at runtime**
   (added 2026-07-14) — builds directly on item 7, sequence after it.
   Needs a picker UI. Worth confirming deliberately (not assuming): what
   happens to an already-assigned note with no friendly name in the new
   list — `gm_note_display()`'s existing "Note N" fallback likely already
   covers this.
9. **Save current settings to a file / load a previous settings file**
   (added 2026-07-14) — the one item here that genuinely needs a design
   pass before being built, not just glue code. This app already has TWO
   overlapping preset concepts — device flash presets (`04 06`) and the
   local "My Presets" JSON (`ui/presets.py`) — plus the already-flagged
   `presets_tab.py` key-name mismatch between them (see SysEx Extension
   section above). A third, file-based save/load bolted on without first
   deciding its relationship to those two would compound that confusion
   rather than fix anything.

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

**Hi-hat as a generic pad-type input (captured 2026-08-06, future direction —
not yet implemented, needs electrical validation first):**
- Goal: remove the dedicated GPIO1/jack-4 hi-hat input entirely. Hi-hat
  becomes a 4th selectable `padType` (alongside `DUAL_PIEZO`/
  `PIEZO_SWITCH_CHOKE`/`SINGLE_PIEZO`), usable on ANY standard jack input —
  not a special-case connector or channel.
- **Open question, must be resolved before implementation: electrical
  compatibility of the standard piezo front-end (1kΩ series + BAT85 clamp +
  1MΩ pull-down + 22nF filter) with a continuous hi-hat position signal.**
  Reasoned through in chat but NOT verified against the actual KiCad
  schematic:
  - No series/blocking capacitor identified in the front-end as documented
    — if that holds, a continuous/DC-ish signal should pass through
    unattenuated (22nF filter cap's ~7.2kHz RC corner is far faster than any
    foot movement). Needs confirming directly in KiCad, not assumed.
  - **FSR-type sensors need a bias/voltage-divider network to produce a
    readable voltage at all** — the current GPIO1 hi-hat input almost
    certainly has this; the standard piezo channels likely do not. If the
    replacement hi-hat controller is FSR-based (vs. an already-conditioned
    voltage output like Roland VH-11/FD-8 style controllers), a bias
    resistor stuffing option would need adding to the standard front end.
  - **Firmware-side conflict identified, independent of the analog
    hardware question:** the existing per-channel DSP chain (DC-offset
    removal → spike-rejection → EMA) is built for transient piezo signals
    — DC-offset removal specifically would treat slow hi-hat pedal movement
    as baseline drift and filter it out. Whichever input is set to the new
    hi-hat pad type must bypass that pipeline and route to the existing
    (already-built) hi-hat consumer — EMA + linear-map + 7-step-quantize —
    instead. The layered architecture (`AdcSampler` → `SampleStream` →
    per-channel consumer) should make this a routing change, not a rewrite,
    since the hi-hat consumer is already architecturally a peer to
    `TriggerEngine`, not built on top of it.
- **Ripple effects if implemented, noted so scope is understood upfront:**
  - `NUM_INPUTS` 5→4, `INPUT_ID` range 00–04→00–03 — a BREAKING SysEx
    protocol change (unlike every extension so far this project, which has
    been additive/append-only), needs its own deliberate version bump, not
    folded silently into another change.
  - LittleFS config blob size changes → `uploadfs`/config reset required,
    same as every other struct change this project has done.
  - App UI: hi-hat currently lives outside the 2×2 grid as its own dedicated
    button + separator + right-panel placeholder stack page. Needs to become
    reachable via the normal pad-selection → Config tab → Type=Hi-Hat path
    instead — a real UX redesign (the dedicated button/separator likely goes
    away), not a small tweak.
  - Nice side effect: once hi-hat is "just a pad type," it's automatically
    available on BT-1 Expand's inputs 4–7 with zero extra design work.
  - The spare second channel on a hi-hat-configured jack (tip, if ring
    carries continuous position) could plausibly carry a pedal open/close
    switch, structurally similar to the existing choke sustained-signal
    detection logic — noted as a possible future extension, not required
    for a first implementation.
- **Not scheduled into the current in-flight satellite PCB revision** —
  deliberately kept separate from the all-JST/USB-C changes above. Electrical
  validation (checking the actual KiCad schematic for a blocking cap, and
  confirming what signal type a real replacement hi-hat controller outputs)
  is the prerequisite next step before any implementation work starts.

**Custom drum triggers (captured 2026-08-06, future direction — research/design
while waiting on satellite fab):**
- Cross-stick/rim-click discrimination is genuinely hard even in commercial
  products, not just a BT-1 firmware tuning gap — reviews of Jobeky's own
  dual-zone side trigger flag cross-stick as a weak point, attributed to the
  piezo being edge/case-mounted rather than in direct head contact.
- Near-term, low-risk build: standalone side/bar trigger (rigid bar or
  bracket at the rim position, own piezo, own wire) as a `SINGLE_PIEZO`
  input on an existing satellite — no new pad-type logic needed. Modelled on
  Jobeky's bar trigger / drum-tec Groovebar.
- Longer-term, bigger scope: full custom drum triggers mounted in existing
  acoustic shells under mesh heads (Jobeky/drum-tec internal side-trigger
  style) — full A2E-style conversion hardware. Electronics side is a natural
  extension of existing dual-piezo sensing work; mechanical/mounting design
  (foam or spoke systems, bearing-edge height, jack routing through the
  shell's air vent) is a new skillset, not yet scoped.
- Advantage if built: tighter control over dialing in module ↔ sensor match
  than off-the-shelf triggers allow.

---

**Dual Independent Piezo — new pad type (captured 2026-08-06, future
direction, fully specified, not yet implemented):**
- Goal: split a dual-zone jack into two fully independent single-piezo
  instruments (e.g. two toms, or a tom + kick), rather than treating the
  second channel as a rim/zone-2 of one instrument.
- **Reuses DUAL_PIEZO's existing field set as-is — NO new SysEx commands,
  fields, or addressing changes needed.** Same `INPUT_ID`, same 19-byte
  `02 07` config response, just a new `padType` enum value. DUAL_PIEZO
  already gives its rim zone independent threshold/sensitivity/curve/note/
  channel via `z2note`/`z2channel` etc. — exactly the field set an
  independent second instrument needs.
- **Firmware: LESS logic than DUAL_PIEZO, not more** — skips ratio
  classification, cross-stick stage 2, and layering suppression entirely.
  Both channels already run independent Scan/Mask windows (existing
  "either channel starts scan" architecture from the snare-rim work); this
  type just always sends both notes rather than routing through the
  classify decision tree.
- More correct model for this use case, not just simpler: ratio-based
  discrimination exists specifically for bleed between zones of ONE
  mechanically-coupled pad (e.g. CY-5's head/rim coupling); two unrelated
  single-piezo pads sharing a jack have no such coupling.
- **UI: no layout change** — same card, same Config/MIDI tabs. New icon:
  a simple generic "two drum" icon (not per-instrument).
- Physical: needs a TRS-to-2×-mono breakout cable/accessory — fold into
  the "standardise a family of connector accessories" idea alongside the
  panel-mount jack breakout and USB-C pigtail already planned.
- Cons/considerations: mutually exclusive with dual-zone use on that jack
  (inherent trade-off, not a bug); channel cross-talk should be verified
  on real hardware given prior history of an ADC-channel-collision bug
  (already fixed elsewhere, but independence assumption worth re-confirming
  empirically for genuinely unrelated pads sharing a jack); shared ground
  reference across the two breakout cable legs likely fine but unverified.

**Secondary/rim label field — generalised across pad types (captured
2026-08-06):**
- Idea: a free-text secondary label field (e.g. "Rimshot", "Cowbell",
  "Bell", "Kick 2"), applicable to ANY pad type with a second output —
  DUAL_PIEZO (rim), PIEZO_SWITCH_CHOKE (`alternateNote`), the new Dual
  Independent Piezo type above, and any future type with a second zone.
  Shown/hidden by `padType` using the same pattern already used throughout
  the Config tab.
- Icon vs label kept as separate concerns: icon still driven by `padType`;
  label is independent text, not type-driven.
- **OPEN QUESTION, must check before designing:** no SysEx command found
  anywhere in `sysex_spec.md` for setting/getting a pad's PRIMARY Name
  field either — Category 02 covers type/threshold/curve/retrigger/
  sensitivity/scan/mask/rim/choke fields, but nothing resembling "set pad
  name." Need to check `SysEx.cpp`/`pad_config_tab.py` directly to
  determine whether Name is local/app-only (e.g. lives in "My Presets"
  JSON) or synced to the device via an undocumented command, before
  designing how the secondary label syncs — don't want to duplicate or
  conflict with however primary Name currently works. (This question
  surfaced directly out of the 2026-08-06 doc-vs-code audit discussion —
  see "Documentation audit" note below.)
- If/when wire encoding is needed: reuse the existing preset-name
  convention (`[NAME_LEN] [NAME_BYTES...]`, ASCII, 16-char cap) rather than
  inventing a new string encoding.
- Open UI question, not blocking: how "Tom 2/Cowbell" actually renders on
  the compact pad card given limited space — likely primary name on card,
  full "Name/Secondary" combo only in the Config tab detail view — decide
  when actually building.

**Documentation audit — flagged as worth doing (2026-08-06):** this single
session surfaced multiple doc-vs-code drift instances (`02 08`/`02 09`/
`02 0A` link/unlink/status commands missing from `sysex_spec.md`'s table
despite being implemented and in active use; the J8/J12 satellite PCB note
turning out to be already resolved; the earlier 2026-07-14 "link/unlink
removed" false claim already corrected once). Also found: the project
knowledge upload of `sysex_spec.md` is a full version behind the live repo
file (v0.1 vs v0.3) — worth re-uploading the current file so future
sessions referencing project knowledge don't work from stale protocol
info. Full audit against `SysEx.cpp`/`sysex.py` completed 2026-08-06 —
see below for findings and fixes.

**SysEx protocol audit findings + fixes (2026-08-06):**
- **Pad type enum table was completely wrong in `sysex_spec.md`** —
  documented a stale 7-value enum (00=piezo, 01=piezo+rim switch, ...,
  06=dual piezo) left over from before the pad-type architecture was
  redesigned. Real firmware enum (confirmed in `PDrumTrigger.h`) is only 3
  values: 0=DUAL_PIEZO, 1=PIEZO_SWITCH_CHOKE, 2=SINGLE_PIEZO. FIXED in doc.
- **`GET_STATUS`'s reserved-check bug, found as a consequence of the above:**
  `SysEx.cpp`'s `SYSEX_PAD_GET_STATUS` handler checked `padType == 1 ||
  padType == 5` using the OLD enum's meaning — meaningless under the current
  3-value enum. **Decision: repurpose `LINK`/`UNLINK`/`GET_STATUS` for the
  deferred ride-bell cross-jack coupling idea** (see "Ride bell" note above)
  rather than retire them — `linkedInput` was already exactly the right
  addressing primitive, just serving a stale purpose. FIXED: renamed
  `SYSEX_INPUT_RESERVED`→`SYSEX_INPUT_LINKED` (firmware `SysEx.h`, Python
  `sysex.py`, and `pad_config_tab.py`'s `INPUT_RESERVED` usages), and
  `GET_STATUS` now reports LINKED simply when `linkedInput != 0xFF`, no
  padType inspection at all. **Not yet built:** the actual coupling
  BEHAVIOUR (bell switch borrowing the ride body's live velocity) — this
  fix only corrects the status/addressing layer, matching how the addressing
  was already correct even though the status logic built on top of it wasn't.
- **`02 08`/`02 09`/`02 0A` (Link/Unlink/Get status) were missing from
  `sysex_spec.md`'s table entirely**, despite being implemented and in
  active use (`pad_config_tab.py`'s refresh worker calls `02 0A` on every
  input load). FIXED — added, with LINKED semantics reflecting the fix above.
- **`05 04` doc was backwards — doc lagging code, not code lagging doc.**
  Documented as "Raw ADC stream — reserved, not yet implemented" with a
  3-byte payload. Actually fully implemented (Hi-Hat Pedal CC v1, see that
  section above) and sent unconditionally on every hi-hat CC change, with a
  **4-byte** payload (`[INPUT_ID] [RAW_HI] [RAW_LO] [CC_VALUE]`) that the
  app's hi-hat calibration UI depends on. FIXED in doc.
- **`crosstalkGroup` (`02 05`) is fully wired (SysEx set/get, presets) but
  never read anywhere in `PDrumTrigger`** — a functionally inert field.
  **Decision: leave wired, flag as inert in the doc, decide later** (not
  retired, not implemented now).
- **`05 02` Input error is fully defined (firmware constant, Python parser
  `parse_input_error`) but never sent** — no firmware code path calls
  `sysexSendResponse` with it, and no error-code vocabulary is defined for
  the `ERROR_CODE` byte. Also has no self-test in `sysex.py`, unlike the
  other Category 05 parsers. **Decision: this is a real near-term direction,
  not just "leave inert"** — concrete motivating use cases already in hand:
  the ADC sampler `ESP_ERR_NO_MEM`/heap-fragmentation bug (Hi-Hat Pedal CC
  v1 section above) is exactly the kind of hardware fault this exists to
  surface to the app, currently only visible via Serial/telnet. Also flagged
  as directly relevant to LittleFS errors (`configLoad`/`configSave`/preset
  I/O currently fail silently to the app) and to the upcoming wireless
  satellite work (pairing failures, link loss, battery-critical — all
  conditions the app has no way to learn about today). **Not yet designed:**
  an actual `ERROR_CODE` enum, and which firmware fault conditions should
  trigger it. Worth designing as its own task alongside — not blocking —
  the ESP-NOW satellite work, since satellite link-health reporting is one
  of its natural first real use cases.

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