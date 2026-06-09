# eDrum Project State
Last updated: 2026-06-09

## Hardware
- Custom PCB, Seeeduino XIAO footprint, MCP3008 SPI ADC
- 4 stereo jacks → 8 MCP3008 channels (inputs 0-3, dual-zone capable)
- 1 mono jack → A0 directly on RP2040 (hi-hat controller, input 4)
- Currently: XIAO RP2040 installed on built PCB
- Stage 2: XIAO ESP32-S3 for wireless satellite modules (deferred)

## Working
- RP2040 firmware boots cleanly, LittleFS config storage working
- USB MIDI enumerates on Windows and Mac
- SysEx protocol v0.2 — full read/write/save round-trip working
- All 9 DSP parameters in firmware and protocol:
  headSensitivity, threshold, scanTime, maskTime, maskTime,
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

## Stage 1 UI — Complete
- 2x2 pad grid (inputs 0-3) + hi-hat controller (input 4) below separator
- Input 4 locked as Hi-Hat Controller (not yet implemented in firmware)
- VelocityCurveWidget: mathematically accurate drawn curves, live hit dot
  (X=raw velocity, Y=MIDI output)
- HitLogWidget: 3-colour bars (teal=head, orange=rim, grey=other pad)
- Vertical sliders for all 7 trigger settings (rim sliders greyed for
  single-zone pads)
- GM percussion dropdowns for MIDI note selection
- MIDI monitor strip showing last hit note/velocity/channel
- Dark theme, drum kit icons (app/assets/pads/ 1024×1024 PNG)
- Pad names persist locally (app/pad_names.json)

## Pending — Next Sessions
- **Real pad testing**: connect pads to inputs 0-3, tune DSP params
- **Hi-hat firmware**: A0 analog read, CC output, open/close thresholds,
  min/max calibration
- **Presets tab**: save/load named configurations
- **MIDI Mapping tab**: per-input note/channel assignment UI
- **curves.py**: shared curve math module (VelocityCurveWidget + emulator)
- **Move project out of Dropbox**: clone to C:\Dev\ to avoid file lock issues

## Protocol
- SysEx v0.2, manufacturer ID 00 7D
- Spec: docs/sysex_spec.md (authoritative)
- PAD_TYPE_DUAL_PIEZO = 0x05 (was 0x06, bass-drum type removed)
- 57 Python self-tests passing

## Key Architecture Decisions
- Stage 1: RP2040 + USB MIDI
- Stage 2: ESP32-S3 satellites, BLE MIDI to head unit (deferred)
- Config storage: LittleFS binary structs
- Python venv: app/venv (Windows), ~/edrum-venv (Mac)
- PyQt6 pinned to 6.4.2 on Mac (Monterey compatibility)
- One jack = one pad (no splitter cable complexity for Stage 1)

## Known Issues / Gotchas
- Windows WinMM: rtmidi callback silently drops SysEx — use polling
- Windows WinMM: echoes sent SysEx back to input — echo filter in transport
- RP2040 upload: 'r' serial command → mbed protocol, or picotool
- LittleFS uploadfs requires mklittlefs in PATH:
  C:\Users\andre\.platformio\packages\tool-mklittlefs-rp2040-earlephilhower
- Mac SSH sessions cannot receive MIDI (no CoreMIDI run loop)
- BLE MIDI SysEx dropped by macOS Monterey (use USB)
- Dropbox sync causes file lock issues during Claude Code sessions
  → plan to move project to C:\Dev\

## Repo
github.com/punky-labs/edrum-project

## Dev Environment
- Windows: VS Code + PlatformIO + Claude Code CLI
- MCP filesystem server: project root mounted for Claude Desktop
- upload_protocol = picotool, upload_port = COM10 (XIAO RP2040)
- board_build.filesystem_size = 0.5m in platformio.ini