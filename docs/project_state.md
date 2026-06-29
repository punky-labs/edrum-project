# eDrum Project State
Last updated: 2026-06-29 (sensing rewrite — Stage 1 complete)

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
  - Tip = head/piezo channel
  - Ring = rim/switch channel
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
- Independent serial connection (115200 baud) to RP2040 USB CDC
- _SerialReader(QThread): state-machine parser for [SCOPE]/T,H,R/CSV protocol
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
- o <input> <floor> — arm scope capture on input
- o off — disarm scope
- w <input> <param> <value> — set DSP param live (thresh/sens/scan/mask/retrig)
- p — send SysEx ping
- i — send SysEx identify request
- n — send test note (C3, ch10)
- r — reboot to bootloader (UF2 upload mode)

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
- **[UNRESOLVED] Hard hit runaway:** On very hard hits (mainly mesh pads),
  the unit occasionally enters a runaway state firing continuous MIDI notes.
  Usually cleared by hitting the pad again; occasionally requires USB replug.
  Suspected cause: ADC saturation at 1023 keeping signal above threshold
  indefinitely, or loopTimes incrementing without scan-end condition firing.
  Mitigation planned: watchdog in sensing loop — if loopTimes exceeds ~500
  iterations, force-reset scan state.
  To reproduce: arm scope on input 1, floor=50, hit as hard as possible
  repeatedly; watch serial for continuous [HIT] lines; scope capture just
  before runaway will show ADC behaviour.

---

## Pending — Next Sessions

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