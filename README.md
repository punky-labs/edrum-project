# eDrum

A custom electronic drum trigger module — open hardware, open firmware, and a companion Python config app.

---

## Overview

eDrum is a DIY electronic drum trigger system built around a custom PCB and the Arduino/PlatformIO ecosystem. It converts piezo and switch signals from drum pads and cymbals into USB MIDI, with full configuration via a desktop app over a custom SysEx protocol.

The project is structured as a monorepo covering hardware schematics, firmware, and the config app.

---

## Hardware

Custom PCB based on the **Seeeduino XIAO** footprint.

- **ADC:** MCP3008 (SPI, 8-channel, 10-bit)
- **Inputs:** 4 × dual-channel jacks (8 piezo/switch inputs) + 1 × hi-hat control input = **9 inputs total**
- **Pad types supported:** single piezo, dual piezo (e.g. Roland mesh pads), piezo + rim switch, rim switch only, hi-hat continuous (CC), hi-hat open/closed switch, bass drum
- **Stage 1 MCU:** XIAO RP2040 — USB MIDI head unit
- **Stage 2 MCU:** XIAO ESP32-S3 — BLE MIDI satellite modules (planned)

Schematics are in `hardware/` (KiCad).

---

## Repository Structure

```
edrum-project/
├── firmware/          # PlatformIO C++ firmware
│   └── src/
│       ├── config/    # InputConfig struct, LittleFS persistence
│       ├── midi/      # SysEx parser/dispatcher, USB MIDI, BLE MIDI
│       ├── pdrum/     # PDrum DSP library (hit detection, velocity curves)
│       └── hal/       # Hardware abstraction (MCP3008, ADC)
├── app/               # Python config application
│   ├── protocol/      # SysEx constants, builders, parsers (sysex.py)
│   ├── transport/     # rtmidi wrapper (midi.py)
│   └── ui/            # PyQt6 interface
├── docs/
│   ├── sysex_spec.md  # SysEx protocol specification (authoritative)
│   └── project_state.md
└── README.md
```

---

## Firmware

**Toolchain:** PlatformIO + Arduino framework

**Stage 1 — RP2040 (current)**

```bash
# Build and upload
cd firmware
pio run -e xiao_rp2040 -t upload
```

The RP2040 target uses TinyUSB for USB MIDI. Uploading requires the board to be in MBED bootloader mode — send `r` over the serial monitor first, then PlatformIO will copy the UF2 to the mounted drive.

**Stage 2 — ESP32-S3 (planned)**

```bash
pio run -e xiao_esp32s3 -t upload
```

The ESP32-S3 target uses BLE MIDI for wireless satellite operation. Not yet in active development.

**Key firmware modules:**

| Module | Description |
|---|---|
| `config/Config.h` | `InputConfig` struct — all per-pad settings |
| `config/Config.cpp` | LittleFS load/save, preset management |
| `midi/SysEx.cpp` | Full SysEx parser and dispatcher |
| `pdrum/pdrum.cpp` | DSP: hit detection, scan/mask timing, velocity curves |

---

## Config App

**Requirements:** Python 3.10+, PyQt6, python-rtmidi

**Setup (Windows):**
```bash
cd app
python -m venv venv
venv\Scripts\pip install -r requirements.txt
```

**Setup (Mac):**
```bash
cd app
python3 -m venv ~/edrum-venv
~/edrum-venv/bin/pip install -r requirements.txt
```

> **Mac note:** PyQt6 is pinned to 6.4.2 for Monterey compatibility. BLE MIDI SysEx is dropped by macOS Monterey CoreMIDI — use USB.

**Run:**
```bash
# Windows
app\venv\Scripts\python.exe app\main.py

# Mac
~/edrum-venv/bin/python app/main.py
```

**Run protocol self-tests:**
```bash
app\venv\Scripts\python.exe app\protocol\sysex.py
```

---

## SysEx Protocol

All configuration uses a custom MIDI SysEx protocol over USB (Stage 1) or BLE MIDI (Stage 2).

```
F0  00 7D  [DEVICE_ID]  [CMD_HIGH]  [CMD_LOW]  [DATA...]  F7
```

- Manufacturer ID: `00 7D` (non-commercial reserved)
- 5 command categories: System, Pad Config, MIDI Mapping, Preset Management, Status
- 14-bit values (thresholds, timings) encoded as two 7-bit bytes, big-endian
- Full specification: [`docs/sysex_spec.md`](docs/sysex_spec.md)

**Per-pad configurable parameters:**

| Parameter | Description |
|---|---|
| Pad type | Single piezo, dual piezo, rim switch, hi-hat, bass drum |
| Threshold | Minimum signal level to trigger a hit |
| Head sensitivity | Upper ADC bound for velocity scaling |
| Scan time | Peak scan window (ms) — affects latency vs. accuracy |
| Mask time | Post-hit ignore window (ms) — prevents double triggers |
| Rim threshold | Rim/zone-2 trigger threshold |
| Rim sensitivity | Rim/zone-2 sensitivity |
| Velocity curve | Natural, Expressive, Sensitive, Punchy, Aggressive, Custom |
| Retrigger time | Additional retrigger suppression (ms) |
| Crosstalk group | Inputs in the same group suppress each other |
| MIDI note | Head zone MIDI note number and channel |
| MIDI note (zone 2) | Rim/edge zone MIDI note and channel |
| CC mapping | Hi-hat continuous control number and channel |

---

## Development Notes

- Config changes are held in RAM and only written to flash on explicit Save (`01 06`) — avoids unnecessary flash wear during adjustment
- `05 03` Hit Event messages are separate from the DAW MIDI stream, used by the app for live velocity display during calibration
- Windows MIDI port names include a trailing number (e.g. `eDrum 1`) — the app strips this automatically
- Windows BLE MIDI requires a virtual loopback driver (e.g. MIDIberry)
- The MCP filesystem MCP server is configured for Claude Desktop at the project root

---

## Stage 2 Roadmap

Stage 2 adds wireless satellite modules (ESP32-S3) that connect to the head unit via BLE MIDI. The SysEx protocol already supports this via the `DEVICE_ID` field (`01`–`0F` for satellites). The Python app requires no changes — the head unit routes SysEx transparently.

---

## License

Hardware, firmware, and app are released under the MIT License. The PDrum DSP library (`firmware/src/pdrum/`) is based on the HelloDrum Arduino Library by Ryo Kosaka.