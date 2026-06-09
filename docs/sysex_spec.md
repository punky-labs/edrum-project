# eDrum Project — MIDI SysEx Protocol Specification
**Version:** 0.2  
**Last updated:** 2026-06-08

---

## Message structure

Every message follows this byte layout:
F0  00 7D  [DEVICE_ID]  [COMMAND_HIGH]  [COMMAND_LOW]  [DATA...]  F7

- `F0` — SysEx start
- `00 7D` — manufacturer ID (non-commercial reserved)
- `DEVICE_ID` — `00` = head unit, `01`–`0F` = satellite modules
- `COMMAND_HIGH` — category byte
- `COMMAND_LOW` — specific command within category
- `DATA` — variable length, command-specific
- `F7` — SysEx end

### 7-bit data encoding

All data bytes in SysEx must be 7-bit (0x00–0x7F). Values above 127
(e.g. 14-bit ADC thresholds, retrigger times) are split into two 7-bit
bytes: high byte first, then low byte.

Example: value 1000 (0x03E8) → `07 68`

---

## Category 01 — System

| Command | Data bytes | Name | Description |
|---|---|---|---|
| `01 01` | none | Ping | App checks if module is alive |
| `01 02` | none | Pong | Module responds to ping |
| `01 03` | none | Identify request | App requests device info |
| `01 04` | `[FW_MAJ] [FW_MIN] [DEVICE_ID] [NUM_INPUTS]` | Identify response | Module reports firmware version, ID, input count |
| `01 05` | none | Reset config | Restore all settings to factory defaults |
| `01 06` | none | Save to flash | Commit current config to NVS flash |
| `01 07` | `[STATUS]` | Ack | `00`=ok, `01`=error, `02`=unknown command |

---

## Category 02 — Pad config

`INPUT_ID` range: `00`–`08` (4 dual-channel inputs + 1 hihat input = 9 inputs).

| Command | Data bytes | Name | Description |
|---|---|---|---|
| `02 01` | `[INPUT_ID] [PAD_TYPE]` | Set pad type | See pad type table below |
| `02 02` | `[INPUT_ID] [THRESH_HI] [THRESH_LO]` | Set threshold | 14-bit value, 7-bit split |
| `02 03` | `[INPUT_ID] [CURVE_TYPE]` | Set velocity curve | See curve type table below |
| `02 04` | `[INPUT_ID] [RETRIG_HI] [RETRIG_LO]` | Set retrigger time | Time in ms, 14-bit split |
| `02 05` | `[INPUT_ID] [XTALK_GROUP]` | Set crosstalk group | Inputs in same group suppress each other |
| `02 06` | `[INPUT_ID]` | Get pad config | Request current config for one input |
| `02 07` | `[INPUT_ID] [PAD_TYPE] [THRESH_HI] [THRESH_LO] [CURVE_TYPE] [RETRIG_HI] [RETRIG_LO] [XTALK_GROUP] [SENS_HI] [SENS_LO] [SCAN_HI] [SCAN_LO] [MASK_HI] [MASK_LO] [RSENS_HI] [RSENS_LO] [RTHRESH_HI] [RTHRESH_LO]` | Pad config response | Full config dump for one input (18 bytes) |
| `02 08` | `[INPUT_A] [INPUT_B]` | Link inputs | Pair two inputs as one instrument (e.g. ride body + bell) |
| `02 09` | `[INPUT_ID]` | Unlink input | Remove input from any linked pair |
| `02 0A` | `[INPUT_ID]` | Get input status | Query whether input is available, active, or reserved |
| `02 0B` | `[INPUT_ID] [SENS_HI] [SENS_LO]` | Set head sensitivity | Upper ADC bound for velocity scaling, 14-bit split |
| `02 0C` | `[INPUT_ID] [SCAN_HI] [SCAN_LO]` | Set scan time | Peak scan window in ms, 14-bit split |
| `02 0D` | `[INPUT_ID] [MASK_HI] [MASK_LO]` | Set mask time | Post-hit ignore window in ms, 14-bit split |
| `02 0E` | `[INPUT_ID] [RSENS_HI] [RSENS_LO]` | Set rim sensitivity | Rim/zone-2 sensitivity, 14-bit split |
| `02 0F` | `[INPUT_ID] [RTHRESH_HI] [RTHRESH_LO]` | Set rim threshold | Rim/zone-2 threshold, 14-bit split |

### Input status response values (02 0A)
00 = available
01 = active (configured)
02 = reserved (paired to another input)

### Pad type values
00 = piezo (single zone)
01 = piezo + rim switch (dual zone) — consumes 2 input channels
02 = rim switch only
03 = hihat control (continuous)
04 = hihat control (open/closed switch)
05 = bass drum (single zone)
06 = dual piezo (head + rim piezo, e.g. newer Roland mesh pads) — consumes 2 input channels

**Note on types 01 and 06:** Both consume two physical input channels.
When either type is assigned to an input, the paired channel is
automatically reserved and unavailable for independent assignment.
The Python config app must query input status (`02 0A`) on load and
grey out any reserved inputs in the UI.

**DSP distinction:**
- Type `01` (piezo + rim switch): uses analog amplitude for velocity,
  digital threshold for zone detection
- Type `06` (dual piezo): uses analog amplitude comparison between two
  piezo signals to determine hit zone and velocity

### Velocity curve type values
00 = Natural    — linear response, what you play is what you get
01 = Expressive — soft bias, easy to play quietly, wide dynamic range
02 = Sensitive  — stronger soft bias, very touch-responsive
03 = Punchy     — loud bias, present even on moderate hits
04 = Aggressive — maximum punch, less dynamic variation
05 = Custom     — reserved for future point-table implementation

---

## Category 03 — MIDI mapping

| Command | Data bytes | Name | Description |
|---|---|---|---|
| `03 01` | `[INPUT_ID] [MIDI_NOTE] [MIDI_CHANNEL]` | Set note mapping | Map input to note + channel |
| `03 02` | `[INPUT_ID] [MIDI_NOTE] [MIDI_CHANNEL]` | Set rim/zone 2 mapping | For dual zone inputs (types 01 and 06) |
| `03 03` | `[INPUT_ID] [CC_NUMBER] [MIDI_CHANNEL]` | Set CC mapping | For hihat continuous control (type 03) |
| `03 04` | `[INPUT_ID]` | Get MIDI mapping | Request current mapping for one input |
| `03 05` | `[INPUT_ID] [MIDI_NOTE] [CH_1] [MIDI_NOTE_2] [CH_2] [CC_NUM] [CC_CH]` | MIDI mapping response | Full mapping dump for one input |

---

## Category 04 — Preset management

Preset names are ASCII, maximum 16 characters, length-prefixed.  
`PRESET_ID` range: `00`–`0F` (16 preset slots on device).

| Command | Data bytes | Name | Description |
|---|---|---|---|
| `04 01` | `[PRESET_ID]` | Load preset | Apply saved preset from flash |
| `04 02` | `[PRESET_ID] [NAME_LEN] [NAME_BYTES...]` | Save preset | Save current config as named preset |
| `04 03` | none | List presets | Request all saved preset IDs and names |
| `04 04` | `[COUNT] [PRESET_ID] [NAME_LEN] [NAME_BYTES...]...` | List presets response | Returns all presets |
| `04 05` | `[PRESET_ID]` | Delete preset | Remove a preset from flash |
| `04 06` | `[PRESET_ID] [ALL_PAD_CONFIG...]` | Export preset | Full preset data dump for Python-side saving (24 bytes per input) |

---

## Category 05 — Response / status

These messages are always device → app direction.

| Command | Data bytes | Name | Description |
|---|---|---|---|
| `05 01` | `[CMD_HIGH] [CMD_LOW] [STATUS]` | Command ack | Confirms receipt of any set command — `00`=ok, `01`=error |
| `05 02` | `[INPUT_ID] [ERROR_CODE]` | Input error | Reports a hardware-level problem on an input |
| `05 03` | `[INPUT_ID] [ZONE] [RAW_VEL] [MIDI_VEL]` | Hit event (debug) | Live hit data. ZONE: 00=head, 01=rim. RAW_VEL: pre-curve sensor velocity (0-127, mapped from ADC). MIDI_VEL: post-curve MIDI output velocity (0-127). |
| `05 04` | `[INPUT_ID] [VALUE_HI] [VALUE_LO]` | Raw ADC stream | Reserved — future raw ADC streaming for hardware calibration (not yet implemented) |

### Zone values (05 03)
00 = head (primary zone)
01 = rim / zone 2

---

## Stage 2 — BLE MIDI routing note

In Stage 2, satellite modules use `DEVICE_ID` `01`–`0F`. The head unit
transparently forwards SysEx to and from the correct satellite based on
`DEVICE_ID`. The Python app already addresses by device ID, so no
app-level changes are required for Stage 2 compatibility.

---

## Design notes

- Config changes are held in RAM during a session and only written to
  NVS flash when `01 06` (Save to flash) is explicitly called. This
  avoids unnecessary flash wear during rapid UI adjustment.
- `05 03` (Hit event) is intentionally separate from the DAW MIDI
  stream. It allows the Python app to display a live velocity meter per
  pad during threshold calibration without interfering with DAW operation.
- All multi-byte values use big-endian 7-bit encoding throughout.
- `05 04` (Raw ADC stream) is reserved for future hardware
  calibration tooling. It will be enabled/disabled on demand by
  the app and is intended for dev use only — not required for
  normal operation.
