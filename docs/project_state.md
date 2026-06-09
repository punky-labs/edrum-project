# eDrum Project State
Last updated: 2026-06-09

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
- MIDI monitor strip showing last hit note/velocity/channel
- Dark theme, drum kit icons (app/assets/pads/ 1024×1024 PNG)
- Pad names persist locally (app/pad_names.json)

## Serial Debug Commands (firmware)
- h — print help + build number
- s — dump full config (all inputs, all DSP params)
- a — toggle continuous ADC channel dump (100ms interval)
- p — send SysEx ping
- i — send SysEx identify request
- n — send test note (C3, ch10)
- r — reboot to bootloader (UF2 upload mode)

## Pending — Next Sessions
- **DSP tuning**: dial in threshold/sensitivity/mask per pad type;
  validate rim detection on PD-7
- **Test all 4 jacks**: confirm consistent behaviour across jacks
- **Hi-hat firmware**: A0 analog read, CC output, open/close thresholds,
  min/max calibration
- **Presets tab**: save/load named configurations
- **MIDI Mapping tab**: per-input note/channel assignment UI
- **curves.py**: shared curve math module (VelocityCurveWidget + emulator)
- **Move project out of Dropbox**: clone to C:\Dev\ to avoid file lock issues

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