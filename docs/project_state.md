# eDrum Project State
Last updated: 2026-06-15 (evening session)

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

## Hardware (BT-1 Stage 1)

- Custom PCB, Seeeduino XIAO footprint, MCP3008 SPI ADC
- 4 stereo TRS jacks → 8 MCP3008 channels (4 jacks, dual-zone capable)
  - Tip = odd ADC channels (head/piezo)
  - Ring = even ADC channels (rim/switch)
- 1 mono jack → A0 directly on RP2040 (hi-hat controller, jack 4, stubbed)
- Currently: XIAO RP2040 installed on built PCB
- Stage 2: XIAO ESP32-S3 for wireless satellite modules (deferred)

---

## Current Hardware Test Status (as of 2026-06-15)

Unit tested end-to-end on Windows (dev) and Mac (Addictive Drums VST).
All four jacks active. USB MIDI working on both platforms.

| Jack | Pad | Type | Status |
|------|-----|------|--------|
| 0 | Lemon 13" Cymbal | PIEZO_SWITCH_CHOKE | Working. Choke confirmed. Minor velocity tuning needed. |
| 1 | Roland PDX-8 | DUAL_PIEZO | Working after mask bug fix. Rim discrimination functional. Fine tuning needed. |
| 2 | Roland KD-80 | SINGLE_PIEZO | Working well. Near-flawless on Mac/AD2. |
| 3 | Unassigned | — | Not yet tested. |

**Key tuning insights from real-world testing:**
- Sensitivity needs to match actual pad ADC output range, not default 800
  (KD-80 needed ~60, Lemon cymbal ~200 — very pad-specific)
- Mask time is critical: too short = retriggering, too long = missed fast hits
- DUAL_PIEZO mask bug (time_hit vs millis()) caused all multiple-trigger
  issues on mesh pads — now fixed
- Scope tool essential for tuning — ms x-axis + scan/mask overlays
  make parameter effects immediately visible
- Some audio latency observed on Windows, likely DAW/driver pipeline;
  Mac performance was clean

---

## Working

- RP2040 firmware boots cleanly, LittleFS config storage working
- USB MIDI enumerates on Windows and Mac
- SysEx protocol v0.2 — full read/write/save round-trip working
- Real pad triggering validated: Roland PD-7 (piezo + rim switch) and
  PDX-8 (mesh head) both triggering correctly on real hardware
- Head and rim zone detection working — soft/hard hits produce correct
  velocity range
- All DSP parameters in firmware and protocol:
  headSensitivity, threshold, scanTime, maskTime,
  rimSensitivity, rimThreshold, velocityCurve, retriggerTime
- 05 03 hit events: 4 bytes [INPUT_ID][ZONE][RAW_VEL][MIDI_VEL]
- velocityRaw/velocityRimRaw captured in PDrum before curve applied
- LittleFS: mounted once in configInit(), deferred save via
  g_save_requested flag to prevent USB lockup during flash write
- Python transport: polling thread (WinMM callback drops SysEx),
  fan-out listener registry, echo filter for WinMM loopback
- PyQt app: full Pad Config tab working with emulator and real device
- File logging: app/logs/edrum.log (rotating, 1MB x3)
- Emulator: app/emulator/ — EmulatorTransport + EmulatorWindow,
  launched via --emulator flag; auto-shows on startup
- Auto-incrementing build number via PlatformIO pre-script
  (firmware/version.txt + scripts/increment_build.py)
  Displayed via 'h' serial command: [eDrum] Build N — ...

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

- One PDrum instance per physical jack (not per ADC channel)
- One InputConfig per jack; z2note/z2channel = rim zone of same jack
- Tip = odd ADC channels = head/piezo; ring = even = rim/switch
- Stage 1: RP2040 + USB MIDI
- Stage 2: ESP32-S3 satellites, BLE MIDI to head unit (deferred)
- Config storage: LittleFS binary structs; blob size tied to NUM_INPUTS —
  changing NUM_INPUTS invalidates saved config (resets to defaults, correct)
- Python venv: app/venv (Windows), ~/edrum-venv (Mac)
- PyQt6 pinned to 6.4.2 on Mac (Monterey compatibility)
- Ring buffer architecture: Core 1 writes raw ADC to ringBuf[8][1024],
  Core 0 reads for sensing — clean separation, no smoothing on Core 1
- Spike rejection in pdrum::sensing() replaces EWMA smoothing
- Scope serial connection is independent of MIDI transport — both can
  coexist but avoid simultaneous heavy traffic (WinMM USB hiccup risk)

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

**Firmware / hardware (priority order):**
1. Hard hit runaway watchdog — add loopTimes safety limit to sensing()
2. Per-pad tuning session — dial in sensitivity/mask/scan for all tested pads
   using scope tool; update presets.json with validated values
3. Hi-hat firmware — A0 analog read, CC output, open/close thresholds
4. Watchdog timer — RP2040 hardware watchdog (separate from sensing watchdog)
5. Jack 3 — connect and test fourth pad input

**App (priority order):**
1. curves.py — shared curve math (VelocityCurveWidget + emulator)
2. IBM Plex font bundling
3. Interface mode preference — replace --dev flag with persistent QSettings
4. Hi-hat controller UI
5. Scope window: fix Ctrl+C copy, MIDI transport warning
6. Autocalibrate (deferred — needs algorithm work first)

**Advanced articulations (deferred — Stage 1.5):**
- Cross-stick: requires separate `rimThreshold` parameter for DUAL_PIEZO pads.
  Rim channel should be able to initiate a scan independently when head is below
  headThreshold but rim exceeds rimThreshold. Three-path logic:
  head+rim both above threshold → ratio discrimination → HEAD or RIM;
  head only → HEAD; rim only → cross-stick RIM note.
  Needs rimThreshold added back to InputConfig for DUAL_PIEZO type.
- Ride bell triggering: Lemon Ride has two jacks — second jack carries switch
  for bell. Needs two inputs allocated to one instrument, config model change.
  Likely shares architecture with cross-stick rimThreshold work.
- Both features share a natural implementation milestone: add rimThreshold back
  to InputConfig as a DUAL_PIEZO-specific parameter alongside the existing
  rimRatioThreshold.
- Migrate home desktop project out of Dropbox to D:\Dev\
- Update MCP server config on home desktop after migration

**Design / brand:**
- BOAL colour palette and identity exploration
- Fusion 360 case design for BT-1 family (stacking, mag connector)
- BT-1 Screen hardware planning (ESP32-S3, 5" capacitive touch, LVGL)
- ClickBox hardware planning (nRF52840, eInk + frontlight, NeoPixel)

---

## Repo

github.com/punky-labs/edrum-project

## Dev Environment

- Windows: VS Code + PlatformIO + Claude Code CLI
- MCP filesystem server: project root mounted for Claude Desktop
- upload_protocol = picotool, upload_port = COM10 (XIAO RP2040)
- board_build.filesystem_size = 0.5m in platformio.ini
- Build number: firmware/version.txt (integer) +
  firmware/scripts/increment_build.py (PlatformIO extra_scripts pre:)