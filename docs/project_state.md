# eDrum Project State
Last updated: 2026-06-12

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
- **ADC Scope** — planned for hardware tuning phase; live ADC channel
  visualiser using 'a' serial command (8 channels, 100ms interval)

---

## Serial Debug Commands (firmware)

- h — print help + build number
- s — dump full config (all inputs, all DSP params)
- a — toggle continuous ADC channel dump (100ms interval)
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

## pdrum Library — Known Gaps (next major task)

- Rim detection: `else if (1)` is hardcoded placeholder — always fires
  as head hit regardless of rim signal
- Choke detection: `else if (0)` — dead code, never reached
- No watchdog timer integration
- Unused HelloDrum legacy members: exTCRT, exFSR, pedalCC, hi-hat flags,
  padtype[]/instrumentName[] arrays defined but never used
- curve() uses pow() on every hit — candidate for lookup table
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

---

## Pending — Next Sessions

**Firmware / hardware:**
- pdrum library review/rewrite — rim detection, choke, watchdog timer
- DSP tuning — threshold/sensitivity/mask per pad type on real hardware
- Test all 4 jacks for consistent behaviour
- Hi-hat firmware — A0 analog read, CC output, open/close thresholds
- Watchdog timer — RP2040 hardware watchdog
- Error handling — graceful recovery, factory reset via header pin

**App:**
- curves.py — shared curve math (VelocityCurveWidget + emulator)
- IBM Plex font bundling — app/assets/fonts/, QFontDatabase.addApplicationFont()
- Interface mode preference — replace --dev flag with persistent QSettings
- Hi-hat controller UI — calibration panel, CC mapping, live position indicator
- Autotrack button — currently too prominent, needs to be small/quiet
- ADC Scope dev tool

**Infrastructure:**
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