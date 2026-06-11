# eDrum Project State
Last updated: 2026-06-11 (session 3)

## Hardware
- Custom PCB, Seeeduino XIAO footprint, MCP3008 SPI ADC
- 4 stereo TRS jacks → 8 MCP3008 channels (4 jacks, dual-zone capable)
  - Tip = odd ADC channels (head/piezo)
  - Ring = even ADC channels (rim/switch)
- 1 mono jack → A0 directly on RP2040 (hi-hat controller, jack 4, stubbed)
- Currently: XIAO RP2040 installed on built PCB
- Stage 2: XIAO ESP32-S3 for wireless satellite modules (deferred)

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
  launched via --emulator flag or Dev menu
- Auto-incrementing build number via PlatformIO pre-script
  (firmware/version.txt + scripts/increment_build.py)
  Displayed via 'h' serial command: [eDrum] Build N — ...

## Stage 1 UI — Complete
- 4 pad tiles (jacks 0-3) + hi-hat controller tile (jack 4)
- Jack 4 locked as Hi-Hat Controller (not yet implemented in firmware)
- VelocityCurveWidget: mathematically accurate drawn curves, live hit dot
  (X=raw velocity, Y=MIDI output)
- HitLogWidget: 3-colour bars (teal=head, orange=rim, grey=other pad)
- Vertical sliders for all 7 trigger settings (rim sliders greyed for
  single-zone pads)
- GM percussion dropdowns for MIDI note selection
- MIDI monitor strip showing last hit note/velocity/channel (monospace bold)
- Pad names persist locally (app/pad_names.json)
- Autotrack button functional — pad selection follows incoming hits
- Presets system: category→model two-dropdown selector inline with
  Name/Type; Apply populates UI only (no device write); Save Current…
  saves to My Presets in app/presets.json
- Dev mode: launch with --dev flag; enables Debug tab, Presets Editor tab,
  manufacturer preset editing; user mode shows only Pad Config tab
- QtAwesome icons on toolbar and key buttons (fa5s family)
- Toolbar: single Connect/Disconnect toggle button with green/red tint;
  Refresh and Save to Flash moved to toolbar (enabled only when connected)
- Input cards: large dim numeral (28px, #3a3a3a) top-left as architectural
  element; pad name below icon; no border; selected state = teal icon recolour
- MIDI Mapping top-level tab removed; MIDI assignment lives in per-pad
  detail panel
- Emulator auto-launches on startup when --emulator flag is passed
- Bug fixes: hit log listener registers on connect (not tab switch);
  emulator window closes cleanly on main window close

## Main Window Architecture — Refactored
- No top-level QTabWidget — PadConfigTab is set directly as central widget
- Clean single-view layout in user mode; no redundant tab chrome
- Dev mode: Presets Editor and Debug Console are floating QMainWindow
  instances launched from Dev menu (lazy creation, persist until app close)
- Dev menu items (dev mode only): Launch Emulator, Presets Editor…,
  Debug Console…
- closeEvent cleans up all floating windows

## Left Panel Layout (current)
- "INPUTS" section label
- 2×2 pad card grid (inputs 0–3)
- HLine separator  ← expansion inputs will insert here (inputs 4–7, deferred)
- HLine separator
- Hi-Hat Controller button (full width, 56px tall, icon + label)
- addStretch()
- AUTOTRACK button

## Hi-Hat Controller Button
- QPushButton#hihat_controller_btn — checkable, full width, 56px height
- Icon from hihat-control.svg via asset_loader (recoloured on select)
- Clicking populates right panel stack page 2 (hi-hat placeholder for now)
- Selecting a pad card unchecks the hi-hat button and vice versa
- _refresh_hihat_btn() updates icon colour: teal when checked, secondary grey otherwise
## Right Panel Layout (current)
- Velocity curve + hit log panels fill full height (minimum 220px each),
  side by side with stretch=1 — primary live feedback area
- Below: trigger settings sliders (stretch=0) + detail tabs (stretch=0)
- Detail tabs: Config (index 0, default), MIDI (index 1),
  Options (index 2, disabled), Advanced (index 3, disabled)
- Config tab: Name + Type (grid row), Preset selector (hbox row)
- MIDI tab: head/rim note assignments, channels, CC mapping, MIDI monitor
- Loading spinner removed — status bar communicates loading state
- Right panel stack: 0=placeholder, 1=pad detail, 2=hi-hat detail (placeholder)

## BOAL Design System — Implemented
- Stylesheet architecture: app/assets/styles/boal_base.qss (design system)
  + app/assets/styles/edrum.qss (product overrides)
- Loaded and applied in app/ui/theme.py via app.setStyleSheet()
- QPalette retained as fallback for widgets not covered by QSS
- Token map documented at top of boal_base.qss — update hex values
  there AND in theme.py colour constants together
- Typography: IBM Plex Sans (UI labels), IBM Plex Mono (numeric readouts);
  currently falling back to Segoe UI — font bundling pending
- Colour palette: bg-base #141414, bg-surface #1e1e1e, bg-card #252525,
  accent #00aabb (teal), accent-rim #cc6600 (orange), warm text #d8d4ce
- No borders on inputs/combos/spinboxes — differentiated by background shade
- No borders on cards — selected state via icon recolour only
- Group boxes: borderless, 10px radius, uppercase spaced title
- Sliders: 4px groove, 12px round handle, filled track below handle
- QSS dynamic properties on InputCard: selected/reserved drive icon colour
  via _icon_color() → load_pad_icon() with COLOR_ACCENT / COLOR_TEXT_SECONDARY
  / COLOR_TEXT_DISABLED

## SVG Icon System — Implemented
- app/assets/pads/ now contains SVG versions of all pad icons
- asset_loader.py: SVG preferred over PNG (tries .svg first, .png fallback)
- Runtime recolouring via QPainter CompositionMode_SourceIn —
  single SVG file rendered at any colour; cache keyed on (name, size, colour)
- SVG requirements: filled paths only (no strokes), transparent background
- Icon colours: normal = #6b6b6b, selected = #00aabb, reserved = #3a3a3a
- All icons exported from Affinity (or similar) with strokes expanded to fills

## Serial Debug Commands (firmware)
- h — print help + build number
- s — dump full config (all inputs, all DSP params)
- a — toggle continuous ADC channel dump (100ms interval)
- p — send SysEx ping
- i — send SysEx identify request
- n — send test note (C3, ch10)
- r — reboot to bootloader (UF2 upload mode)

## Pending — Next Sessions
- **pdrum library review/rewrite**: biggest blocker for playable pads;
  known gaps: rim detection (hardcoded `else if (1)`), dead choke code,
  unused HelloDrum legacy members; no watchdog timer in firmware
- **DSP tuning**: dial in threshold/sensitivity/mask per pad type;
  validate rim detection on PD-7; requires hardware session
- **Test all 4 jacks**: confirm consistent behaviour across jacks
- **Hi-hat firmware**: A0 analog read, CC output, open/close thresholds,
  min/max calibration (FSR-based custom controller)
- **curves.py**: shared curve math module (VelocityCurveWidget + emulator)
- **Move project out of Dropbox**: clone to C:\Dev\ to avoid file lock issues
- **Watchdog timer**: add RP2040 hardware watchdog to prevent mid-session lockups
- **Error handling hardening**: Windows MIDI/Serial crash scenarios,
  graceful recovery from flash write interruption, factory reset via header pin button
- **IBM Plex font bundling**: add font files to app/assets/fonts/, load via
  QFontDatabase.addApplicationFont() in theme.py; remove Segoe UI fallback
- **Autotrack button**: currently too visually prominent (full-width, teal);
  needs to be small and quiet — low priority cosmetic
- **Hi-hat controller UI**: right panel stack page 2 is a placeholder;
  needs calibration panel (min/max range), CC mapping, open/close thresholds,
  live position indicator; firmware implementation also pending
- **Expansion board**: 4 more jacks (inputs 4–7), same hardware as base;
  left panel grid expands to 2×4; expansion inputs slot in above hi-hat
  separator; firmware and protocol changes required
- **Tab chrome**: single-tab QTabWidget in per-pad MIDI/Options/Advanced
  section — Options and Advanced are placeholder/disabled; revisit when
  those panels are implemented
- **BOAL brand**: colour palette and identity exploration deferred;
  ClickBox product concept live in Notion

## Protocol
- SysEx v0.2, manufacturer ID 00 7D
- Spec: docs/sysex_spec.md (authoritative)
- NUM_INPUTS = 5 (4 jacks + 1 hi-hat)
- INPUT_ID range: 00–04
- Link/unlink/input-status commands removed (02 08, 02 09, 02 0A)
- 57 Python self-tests passing

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

## Default DSP Values (as of refactor)
- threshold: 30, headSensitivity: 500, scanTime: 10, maskTime: 30
- rimThreshold: 30, rimSensitivity: 200, velocityCurve: 0 (linear)
- midiChannel: 10, zone2MidiChannel: 10
- Jack 0: note=36 (kick), z2=36
- Jack 1: note=38 (snare head), z2=40 (snare rim)
- Jack 2: note=42 (hi-hat closed), z2=46 (hi-hat open)
- Jack 3: note=51 (ride), z2=53 (ride bell)
- Jack 4: note=44 (hi-hat pedal CC), stubbed

## pdrum Library — Known Gaps (next major task)
- Rim detection logic: `else if (1)` is a hardcoded placeholder — always
  fires as head hit regardless of rim signal
- Choke detection: `else if (0)` — dead code, never reached
- No watchdog timer integration
- Unused HelloDrum legacy members: exTCRT, exFSR, pedalCC, hi-hat flags,
  padtype[]/instrumentName[] arrays defined but never used by class
- curve() uses pow() on every hit — candidate for lookup table
- HelloDrum reference: github.com/RyoKosaka/HelloDrum-arduino-Library
  (v0.7.7) — useful for known-good DSP parameter values per pad model

## Known Issues / Gotchas
- Windows WinMM: rtmidi callback silently drops SysEx — use polling
- Windows WinMM: echoes sent SysEx back to input — echo filter in transport
- RP2040 upload: 'r' serial command → bootloader, or picotool
- LittleFS uploadfs requires mklittlefs in PATH:
  C:\Users\andre\.platformio\packages\tool-mklittlefs-rp2040-earlephilhower
- Changing NUM_INPUTS or InputConfig struct requires re-uploading filesystem
- Mac SSH sessions cannot receive MIDI (no CoreMIDI run loop)
- BLE MIDI SysEx dropped by macOS Monterey (use USB)
- Dropbox sync causes file lock issues during Claude Code sessions
  → plan to move project to C:\Dev\
- PlatformIO build script: use env["PROJECT_DIR"] not __file__ (SCons context)
- version.txt must not be empty — must contain an integer

## Repo
github.com/punky-labs/edrum-project

## Dev Environment
- Windows: VS Code + PlatformIO + Claude Code CLI
- MCP filesystem server: project root mounted for Claude Desktop
- upload_protocol = picotool, upload_port = COM10 (XIAO RP2040)
- board_build.filesystem_size = 0.5m in platformio.ini
- Build number: firmware/version.txt (integer) +
  firmware/scripts/increment_build.py (PlatformIO extra_scripts pre:)