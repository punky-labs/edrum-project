# eDrum Project — MIDI SysEx Protocol Specification
**Version:** 0.3.1
**Last updated:** 2026-08-06 — doc-vs-code audit: fixed the pad type enum
(was a stale 7-value table, now the real 0/1/2), added missing `02 08`/
`02 09`/`02 0A` rows, corrected `05 04`'s payload/status, flagged
`crosstalkGroup` (inert) and `05 02` (defined, never sent) explicitly.
No wire-format changes — corrections to documentation only, except for
the `LINK`/`UNLINK`/`GET_STATUS` status-value rename (`RESERVED`→`LINKED`,
value unchanged at `0x02`) which mirrors an actual firmware/app rename.

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

`INPUT_ID` range: `00`–`04` (4 physical jacks + 1 hi-hat controller = 5 inputs).

| Command | Data bytes | Name | Description |
|---|---|---|---|
| `02 01` | `[INPUT_ID] [PAD_TYPE]` | Set pad type | See pad type table below |
| `02 02` | `[INPUT_ID] [THRESH_HI] [THRESH_LO]` | Set threshold | 14-bit value, 7-bit split |
| `02 03` | `[INPUT_ID] [CURVE_TYPE]` | Set velocity curve | See curve type table below |
| `02 04` | `[INPUT_ID] [RETRIG_HI] [RETRIG_LO]` | Set retrigger time | Time in ms, 14-bit split |
| `02 05` | `[INPUT_ID] [XTALK_GROUP]` | Set crosstalk group | **Wired but functionally inert (flagged 2026-08-06):** the value round-trips correctly (set/get/preset export) but is never read anywhere in the sensing engine (`PDrumTrigger`) — no crosstalk suppression logic exists. Left wired deliberately; decide whether to implement or retire later. |
| `02 06` | `[INPUT_ID]` | Get pad config | Request current config for one input |
| `02 07` | `[INPUT_ID] [PAD_TYPE] [THRESH_HI] [THRESH_LO] [CURVE_TYPE] [RETRIG_HI] [RETRIG_LO] [XTALK_GROUP] [SENS_HI] [SENS_LO] [SCAN_HI] [SCAN_LO] [MASK_HI] [MASK_LO] [RRATIO_HI] [RRATIO_LO] [CHOKETHRESH_HI] [CHOKETHRESH_LO] [CHOKE_EN]` | Pad config response | Full config dump for one input (19 bytes). `RRATIO` = DUAL_PIEZO rim ratio threshold (ratio×100). `CHOKETHRESH` = PIEZO_SWITCH_CHOKE switch threshold (ADC units). `CHOKE_EN` = choke enabled (0/1). |
| `02 08` | `[INPUT_A] [INPUT_B]` | Link inputs | Pairs two inputs bidirectionally (`linkedInput` set on both). Repurposed 2026-08-06 for the deferred ride-bell cross-jack coupling idea — addressing/status only; the actual coupling behaviour (bell switch borrowing the ride body's live velocity) is not yet built. |
| `02 09` | `[INPUT_ID]` | Unlink input | Clears the link on this input and its partner |
| `02 0A` | `[INPUT_ID]` | Get input status | Query whether input is available, active, or linked — see status values below |
| `02 0B` | `[INPUT_ID] [SENS_HI] [SENS_LO]` | Set head sensitivity | Upper ADC bound for velocity scaling, 14-bit split |
| `02 0C` | `[INPUT_ID] [SCAN_HI] [SCAN_LO]` | Set scan time | Peak scan window in ms, 14-bit split (v3: repurposed as Scan's hard-cap ms — see below) |
| `02 0D` | `[INPUT_ID] [MASK_HI] [MASK_LO]` | Set mask time | Post-hit ignore window in ms, 14-bit split |
| `02 0E` | `[INPUT_ID] [RRATIO_HI] [RRATIO_LO]` | Set rim ratio threshold | **Repurposed 2026-06 from its original "rim sensitivity" name** — sets `rimRatioThreshold` (DUAL_PIEZO classify gate, ratio×100), 14-bit split. NOT the same field as `rimSensitivity` added in `02 15` below — see that command's note. |
| `02 0F` | `[INPUT_ID] [CHOKETHRESH_HI] [CHOKETHRESH_LO]` | Set choke threshold | **Repurposed 2026-06 from its original "rim threshold" name** — sets `chokeThreshold` (PIEZO_SWITCH_CHOKE switch level, ADC units), 14-bit split. NOT the same field as `rimThreshold` added in `02 14` below — see that command's note. |
| `02 10` | `[INPUT_ID] [ENABLED]` | Set choke enabled | `chokeEnabled` (PIEZO_SWITCH_CHOKE), 0/1. **Command byte existed since this table's 0x0B–0x0F block was added, but had no firmware handler until 2026-07-14 — the app's "Choke" checkbox was silently failing until this fix.** |

### Secondary Trigger Behaviours v1 + Scan v3 tunables (added 2026-07-14)

Twelve fields added to firmware 2026-07-12 (telnet-`w`-only until this SysEx
exposure). Three apply to every pad type (Scan v3, not gated by `padType`);
nine are DUAL_PIEZO- or PIEZO_SWITCH_CHOKE-specific. `02 1D`/`02 1E` is a
bundled GET/response for all twelve, mirroring the `02 06`/`02 07` pattern
rather than requiring twelve separate round-trips. These fields are also
appended (same order, 19 bytes) to the end of each per-input record in the
`04 06` preset export — see that command's note.

| Command | Data bytes | Name | Description |
|---|---|---|---|
| `02 11` | `[INPUT_ID] [MARGIN_HI] [MARGIN_LO]` | Set scan margin | `scanMargin` — raw ADC counts, Scan confirmation prominence. Applies to ALL pad types. 14-bit split. |
| `02 12` | `[INPUT_ID] [WAIT_HI] [WAIT_LO]` | Set settle wait | `settleWaitMs` — Scan settle-exit wait in ms. Applies to ALL pad types. 14-bit split. |
| `02 13` | `[INPUT_ID] [ALPHA]` | Set EMA alpha | `emaAlpha` — EMA smoothing alpha ×100 (0–100). Applies to ALL pad types. Single byte. |
| `02 14` | `[INPUT_ID] [RTHRESH_HI] [RTHRESH_LO]` | Set rim threshold | `rimThreshold` (DUAL_PIEZO) — rim's own independent fire gate, raw ADC. Distinct from `rimRatioThreshold` (`02 0E`) — this is the prerequisite floor gate, that's the head-vs-rim classify ratio. 14-bit split. |
| `02 15` | `[INPUT_ID] [RSENS_HI] [RSENS_LO]` | Set rim sensitivity | `rimSensitivity` (DUAL_PIEZO) — rim's own independent velocity-scaling upper bound, raw ADC. Distinct from the legacy `02 0E` slot. 14-bit split. |
| `02 16` | `[INPUT_ID] [CURVE_TYPE]` | Set rim curve | `rimCurve` (DUAL_PIEZO) — same curve enum as `02 03`, applied to the rim zone independently of the head's curve. Single byte. |
| `02 17` | `[INPUT_ID] [NOTE]` | Set cross-stick note | `crossStickNote` (DUAL_PIEZO) — MIDI note for a soft (cross-stick) rim hit. Single byte. |
| `02 18` | `[INPUT_ID] [VELOCITY]` | Set cross-stick cutoff | `crossStickCutoff` (DUAL_PIEZO) — **MIDI VELOCITY units 0–127, NOT raw ADC** (deliberately different domain from every other threshold/margin in this protocol). Below this curved rim velocity → cross-stick note; above → normal rim note. Single byte. |
| `02 19` | `[INPUT_ID] [NOTE]` | Set alternate note | `alternateNote` (PIEZO_SWITCH_CHOKE) — MIDI note sent instead of the head note when a hit coincides with an already-elevated switch channel. Single byte. |
| `02 1A` | `[INPUT_ID] [MINVEL_HI] [MINVEL_LO]` | Set alt-note min velocity | `minAltNoteVelocity` (PIEZO_SWITCH_CHOKE) — min head peak (raw ADC) required to qualify for the alternate note. 14-bit split. |
| `02 1B` | `[INPUT_ID] [HOLD_HI] [HOLD_LO]` | Set choke hold time | `chokeHoldMs` (PIEZO_SWITCH_CHOKE) — sustain time in ms to confirm a choke (real-world target ~500ms). 14-bit split. |
| `02 1C` | `[INPUT_ID] [GRACE_HI] [GRACE_LO]` | Set choke release grace | `chokeReleaseGraceMs` (PIEZO_SWITCH_CHOKE) — release-side debounce window in ms; a dip below `chokeThreshold` shorter than this doesn't reset the hold accumulator. 14-bit split. |
| `02 1D` | `[INPUT_ID]` | Get extended pad config | Request the 12 fields above for one input, bundled (mirrors `02 06`). |
| `02 1E` | `[INPUT_ID] [MARGIN_HI] [MARGIN_LO] [WAIT_HI] [WAIT_LO] [ALPHA] [RTHRESH_HI] [RTHRESH_LO] [RSENS_HI] [RSENS_LO] [RCURVE] [XSTICK_NOTE] [XSTICK_CUTOFF] [ALT_NOTE] [MINVEL_HI] [MINVEL_LO] [HOLD_HI] [HOLD_LO] [GRACE_HI] [GRACE_LO]` | Extended pad config response | Response to `02 1D` (20 bytes). Field order matches the SET commands above. |

### Input status response values (02 0A)
00 = available
01 = active (configured)
02 = linked (paired via `02 08` for cross-jack coupling — renamed from
     "reserved" 2026-08-06; the old per-channel-reservation meaning no
     longer applies under the current one-`InputConfig`-per-jack model)

### Pad type values

**Corrected 2026-08-06 — the table previously here (00=piezo, 01=piezo+rim
switch, 02=rim switch only, 03/04=hihat variants, 05=bass drum, 06=dual
piezo) was stale, left over from before the pad-type architecture was
redesigned, and did not match the firmware. Confirmed directly against
`PDrumTrigger.h`'s own comment ("padType encoding matches the firmware-wide
'settled design'... 0=DUAL_PIEZO, 1=PIEZO_SWITCH_CHOKE, 2=SINGLE_PIEZO").**

00 = DUAL_PIEZO — head piezo + rim piezo (e.g. Roland PDX-8, PDX-12).
     Independent head/rim detection with layering + cross-stick — see
     project_state.md "Secondary Trigger Behaviours v1" for full spec.
01 = PIEZO_SWITCH_CHOKE — head piezo + rim mechanical switch (e.g. Roland
     CY-5, PD-7, cymbals). Switch channel does choke detection (sustained
     signal) + optional alternate-note (transient coincidence), not a
     second MIDI zone.
02 = SINGLE_PIEZO — head piezo only, no rim sensor (e.g. Roland KD-80)

Input 4 (hi-hat controller) does not use `padType` at all — it has no
`TriggerEngine` and is handled by a separate continuous-position code path
(see "Hi-Hat Pedal CC v1"). The app-side Python constants `PAD_TYPE_HIHAT_CC`
(0x03) / `PAD_TYPE_HIHAT_SW` (0x04) exist only as internal UI sentinels for
driving input 4's widget visibility — they are never sent over SysEx as a
real `padType` value (`pad_config_tab.py` explicitly skips sending
`PAD_SET_TYPE` for input 4). Not a wire-protocol concern, but worth knowing
if reading the app code causes confusion about the enum's real range.

**DSP distinction:**
- Type `01` (PIEZO_SWITCH_CHOKE): uses analog amplitude for head velocity,
  sustained-signal monitoring (not peak scan) for choke on the switch channel
- Type `00` (DUAL_PIEZO): independent analog amplitude scaling on both head
  and rim channels, each with its own threshold/sensitivity/curve

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
| `04 06` | `[PRESET_ID] [ALL_PAD_CONFIG...]` | Export preset | Full preset data dump for Python-side saving (43 bytes per input as of 2026-07-14, grown from 24 — append-only, so the first 24 bytes of each record are unchanged. The appended 19 bytes are the Secondary Trigger Behaviours v1 + Scan v3 fields, same order/encoding as the `02 1E` response body minus its leading `INPUT_ID` byte.) |

---

## Category 05 — Response / status

These messages are always device → app direction.

| Command | Data bytes | Name | Description |
|---|---|---|---|
| `05 01` | `[CMD_HIGH] [CMD_LOW] [STATUS]` | Command ack | Confirms receipt of any set command — `00`=ok, `01`=error |
| `05 02` | `[INPUT_ID] [ERROR_CODE]` | Input error | **Defined but never sent (flagged 2026-08-06).** Constant and Python parser exist; no firmware code path currently detects a fault and calls this. No `ERROR_CODE` vocabulary defined yet either. Real near-term direction, not abandoned — concrete motivating cases: the ADC sampler heap-fragmentation bug (see "Hi-Hat Pedal CC v1"), LittleFS read/write failures (`configLoad`/`configSave`/presets currently fail silently to the app), and upcoming satellite link-health reporting (pairing failure, link loss, battery-critical) once ESP-NOW satellites exist. |
| `05 03` | `[INPUT_ID] [ZONE] [RAW_VEL] [MIDI_VEL]` | Hit event (debug) | Live hit data. ZONE: 00=head, 01=rim. RAW_VEL: pre-curve sensor velocity (0-127, mapped from ADC). MIDI_VEL: post-curve MIDI output velocity (0-127). |
| `05 04` | `[INPUT_ID] [RAW_HI] [RAW_LO] [CC_VALUE]` | Hi-hat position event | **Corrected 2026-08-06 — doc previously said "Raw ADC stream, reserved, not yet implemented" with a 3-byte payload; both were wrong.** Fully implemented as of Hi-Hat Pedal CC v1 (2026-07-25) — sent unconditionally on every hi-hat CC change (mirrors `05 03`'s always-on pattern). 4-byte payload: `INPUT_ID` is always `04`; `RAW_HI`/`RAW_LO` = 14-bit raw ADC pedal position (0–4095 in practice); `CC_VALUE` = the quantized 0–127 CC value just sent. The app's hi-hat calibration UI depends on this. |

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
- `05 04` (Hi-hat position event) is sent unconditionally alongside every
  hi-hat CC output, mirroring `05 03`'s always-on pattern — the app's
  calibration UI depends on it, not just a debug-gated print.
