# eDrum Project State
Last updated: 2026-06-08

## Hardware
- Custom PCB, Seeeduino XIAO footprint, MCP3008 SPI ADC
- 4 dual-channel inputs + 1 hihat input (9 total)
- Currently: XIAO RP2040 installed on built PCB
- Stage 2: XIAO ESP32-S3 for wireless satellite modules

## Working
- RP2040 firmware boots cleanly
- USB MIDI enumerates on Windows and Mac
- LittleFS config storage (defaults on first boot)
- SysEx parser and dispatcher (all 4 categories)
- Serial debug commands (p/i/s/n/r)
- NeoPixel LED status
- Python SysEx protocol layer (full round-trip tests passing)
- Python MIDI transport layer (rtmidi wrapper)
- PyQt app scaffold — connect/disconnect/identify working
- Windows port name cleanup (trailing number strip)
- MCP filesystem server configured for Claude Desktop

## In Progress
- PyQt Pad Config tab (next task)

## Pending / Blocked
- MCP3008 HAL layer — needs PCB wired to RP2040
- DSP layer (hit detection, velocity curves) — needs PCB
- Pad Config UI wired to real SysEx commands
- MIDI Mapping UI tab
- Presets UI tab

## Key Architecture Decisions
- Stage 1: RP2040 + USB MIDI (ESP32-S3 USB MIDI unresolvable on Windows)
- Stage 2: ESP32-S3 satellites, BLE MIDI to head unit only
- Config protocol: MIDI SysEx (manufacturer ID 00 7D)
- Config storage: LittleFS binary structs on RP2040
- Python venv: app/venv (Windows), ~/edrum-venv (Mac)
- PyQt6 pinned to 6.4.2 on Mac (Monterey compatibility)

## Known Issues
- Windows requires MIDIberry or similar for BLE MIDI
- RP2040 upload requires 'r' serial command then mbed protocol
- Mac SSH sessions cannot receive MIDI (no CoreMIDI run loop)
- BLE MIDI SysEx dropped by macOS Monterey Core MIDI (workaround: use USB)

## Repo
github.com/punky-labs/edrum-project

## Dev Environment
- Windows: VS Code + PlatformIO + Claude Code CLI
- Mac: ~/edrum-venv, shared app/ folder via network
- MCP filesystem server: project root mounted read-only for Claude Desktop