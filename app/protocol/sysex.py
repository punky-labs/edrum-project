"""
eDrum SysEx protocol — constants, builders, and parsers.

Wire format (bytes, F0 and F7 inclusive):
    F0  00 7D  [DEVICE_ID]  [CMD_HIGH]  [CMD_LOW]  [DATA...]  F7

Mirrors firmware/src/midi/SysEx.h exactly; keep the two files in sync.
"""

from __future__ import annotations

from typing import Optional

# ---------------------------------------------------------------------------
# Framing constants
# ---------------------------------------------------------------------------

SYSEX_START = 0xF0
SYSEX_END   = 0xF7
MFR_0       = 0x00
MFR_1       = 0x7D

DEV_HEAD    = 0x00      # head unit device ID; satellites are 0x01–0x0F

HEADER_LEN  = 5         # MFR0 MFR1 DEV_ID CMD_HI CMD_LO — matches firmware SYSEX_HEADER_LEN
MSG_MIN_LEN = 7         # F0 + HEADER_LEN + F7

NUM_INPUTS  = 9
MAX_PRESETS = 16

# ---------------------------------------------------------------------------
# Category bytes
# ---------------------------------------------------------------------------

CAT_SYS    = 0x01
CAT_PAD    = 0x02
CAT_MIDI   = 0x03
CAT_PRESET = 0x04
CAT_STATUS = 0x05

# ---------------------------------------------------------------------------
# Category 01 — System
# ---------------------------------------------------------------------------

SYS_PING       = 0x01
SYS_PONG       = 0x02
SYS_IDENT_REQ  = 0x03
SYS_IDENT_RESP = 0x04
SYS_RESET      = 0x05
SYS_SAVE       = 0x06
SYS_ACK        = 0x07

# ---------------------------------------------------------------------------
# Category 02 — Pad config
# ---------------------------------------------------------------------------

PAD_SET_TYPE   = 0x01
PAD_SET_THRESH = 0x02
PAD_SET_CURVE  = 0x03
PAD_SET_RETRIG = 0x04
PAD_SET_XTALK  = 0x05
PAD_GET        = 0x06
PAD_RESP       = 0x07
PAD_LINK       = 0x08
PAD_UNLINK     = 0x09
PAD_GET_STATUS     = 0x0A
PAD_SET_SENS       = 0x0B
PAD_SET_SCAN       = 0x0C
PAD_SET_MASK       = 0x0D
PAD_SET_RIM_SENS   = 0x0E
PAD_SET_RIM_THRESH = 0x0F

# Pad type values
PAD_TYPE_PIEZO        = 0x00   # Single piezo (head only)
PAD_TYPE_PIEZO_RIM    = 0x01   # Piezo + switch (head + rim)
PAD_TYPE_RIM_ONLY     = 0x02   # Switch only
PAD_TYPE_HIHAT_CC     = 0x03   # Hi-hat continuous controller
PAD_TYPE_HIHAT_SW     = 0x04   # Hi-hat open/close switch
PAD_TYPE_DUAL_PIEZO   = 0x05   # Dual piezo (e.g. mesh head)

PAD_TYPE_NAMES: dict[int, str] = {
    PAD_TYPE_PIEZO:      "Single Piezo",
    PAD_TYPE_PIEZO_RIM:  "Piezo + Switch",
    PAD_TYPE_RIM_ONLY:   "Switch Only",
    PAD_TYPE_HIHAT_CC:   "Hi-Hat (Continuous)",
    PAD_TYPE_HIHAT_SW:   "Hi-Hat (Switch)",
    PAD_TYPE_DUAL_PIEZO: "Dual Piezo",
}

# Velocity curve values
CURVE_NATURAL    = 0x00
CURVE_EXPRESSIVE = 0x01
CURVE_SENSITIVE  = 0x02
CURVE_PUNCHY     = 0x03
CURVE_AGGRESSIVE = 0x04
CURVE_CUSTOM     = 0x05

CURVE_NAMES: dict[int, str] = {
    CURVE_NATURAL:    "Natural",
    CURVE_EXPRESSIVE: "Expressive",
    CURVE_SENSITIVE:  "Sensitive",
    CURVE_PUNCHY:     "Punchy",
    CURVE_AGGRESSIVE: "Aggressive",
    CURVE_CUSTOM:     "Custom",
}

# Input status values (02 0A response)
INPUT_AVAIL    = 0x00
INPUT_ACTIVE   = 0x01
INPUT_RESERVED = 0x02

INPUT_STATUS_NAMES: dict[int, str] = {
    INPUT_AVAIL:    "available",
    INPUT_ACTIVE:   "active",
    INPUT_RESERVED: "reserved",
}

# Zone values (05 03 hit event)
ZONE_HEAD = 0x00
ZONE_RIM  = 0x01

ZONE_NAMES: dict[int, str] = {
    ZONE_HEAD: "head",
    ZONE_RIM:  "rim",
}

# ---------------------------------------------------------------------------
# Category 03 — MIDI mapping
# ---------------------------------------------------------------------------

MIDI_SET_NOTE = 0x01
MIDI_SET_Z2   = 0x02
MIDI_SET_CC   = 0x03
MIDI_GET      = 0x04
MIDI_RESP     = 0x05

# ---------------------------------------------------------------------------
# Category 04 — Preset management
# ---------------------------------------------------------------------------

PRE_LOAD        = 0x01
PRE_SAVE        = 0x02
PRE_LIST        = 0x03
PRE_LIST_R      = 0x04
PRE_DELETE      = 0x05
PRE_EXPORT      = 0x06

PRESET_NAME_MAX = 16

# ---------------------------------------------------------------------------
# Category 05 — Status / response
# ---------------------------------------------------------------------------

STAT_ACK       = 0x01
STAT_INP_ERR   = 0x02
STAT_HIT_DEBUG = 0x03

# Ack status values
ACK_OK      = 0x00
ACK_ERROR   = 0x01
ACK_UNKNOWN = 0x02

ACK_STATUS_NAMES: dict[int, str] = {
    ACK_OK:      "ok",
    ACK_ERROR:   "error",
    ACK_UNKNOWN: "unknown command",
}

# SysEx-safe sentinel for "no linked input" (firmware stores 0xFF internally)
LINKED_NONE = 0x7F

# ---------------------------------------------------------------------------
# 7-bit encode / decode
# ---------------------------------------------------------------------------

def encode_14bit(value: int) -> tuple[int, int]:
    """
    Split a 14-bit integer into two 7-bit SysEx bytes (hi, lo).
    Example: 1000 (0x03E8) -> (0x07, 0x68)
    """
    if not 0 <= value <= 0x3FFF:
        raise ValueError(f"Value {value} out of 14-bit range (0–16383)")
    return (value >> 7) & 0x7F, value & 0x7F


def decode_14bit(hi: int, lo: int) -> int:
    """Reconstruct an integer from two 7-bit SysEx bytes."""
    return ((hi & 0x7F) << 7) | (lo & 0x7F)


# ---------------------------------------------------------------------------
# Core message builder and parser
# ---------------------------------------------------------------------------

def build_message(
    device_id: int,
    cmd_high: int,
    cmd_low: int,
    payload: Optional[bytes | bytearray | list[int]] = None,
) -> bytearray:
    """Construct a complete SysEx message including F0 framing and F7 terminator."""
    msg = bytearray([SYSEX_START, MFR_0, MFR_1, device_id, cmd_high, cmd_low])
    if payload:
        msg.extend(payload)
    msg.append(SYSEX_END)
    return msg


def parse_message(data: bytes | bytearray) -> Optional[dict]:
    """
    Validate and parse a raw SysEx message (F0 … F7 inclusive).

    Returns a dict with keys: device_id, cmd_high, cmd_low, payload (bytes).
    Returns None if the message is structurally invalid.
    """
    if len(data) < MSG_MIN_LEN:
        return None
    if data[0] != SYSEX_START or data[-1] != SYSEX_END:
        return None
    if data[1] != MFR_0 or data[2] != MFR_1:
        return None
    return {
        "device_id": data[3],
        "cmd_high":  data[4],
        "cmd_low":   data[5],
        "payload":   bytes(data[6:-1]),
    }


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _build(cmd_high: int, cmd_low: int,
           payload=None, device_id: int = DEV_HEAD) -> bytearray:
    return build_message(device_id, cmd_high, cmd_low, payload)


def _check_input_id(input_id: int) -> None:
    if not 0 <= input_id <= 8:
        raise ValueError(f"input_id {input_id} out of range (0–8)")


def _check_preset_id(preset_id: int) -> None:
    if not 0 <= preset_id <= 15:
        raise ValueError(f"preset_id {preset_id} out of range (0–15)")


def _require_len(payload: bytes, minimum: int, context: str) -> None:
    if len(payload) < minimum:
        raise ValueError(f"{context}: payload too short ({len(payload)} < {minimum})")


# ---------------------------------------------------------------------------
# Category 01 builders — System
# ---------------------------------------------------------------------------

def build_ping(device_id: int = DEV_HEAD) -> bytearray:
    return _build(CAT_SYS, SYS_PING, device_id=device_id)


def build_identify_request(device_id: int = DEV_HEAD) -> bytearray:
    return _build(CAT_SYS, SYS_IDENT_REQ, device_id=device_id)


def build_reset_config(device_id: int = DEV_HEAD) -> bytearray:
    return _build(CAT_SYS, SYS_RESET, device_id=device_id)


def build_save_to_flash(device_id: int = DEV_HEAD) -> bytearray:
    return _build(CAT_SYS, SYS_SAVE, device_id=device_id)


# ---------------------------------------------------------------------------
# Category 02 builders — Pad config
# ---------------------------------------------------------------------------

def build_set_pad_type(input_id: int, pad_type: int,
                       device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    return _build(CAT_PAD, PAD_SET_TYPE, [input_id, pad_type], device_id)


def build_set_threshold(input_id: int, value: int,
                        device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    hi, lo = encode_14bit(value)
    return _build(CAT_PAD, PAD_SET_THRESH, [input_id, hi, lo], device_id)


def build_set_velocity_curve(input_id: int, curve_type: int,
                              device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    return _build(CAT_PAD, PAD_SET_CURVE, [input_id, curve_type], device_id)


def build_set_retrigger_time(input_id: int, ms: int,
                              device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    hi, lo = encode_14bit(ms)
    return _build(CAT_PAD, PAD_SET_RETRIG, [input_id, hi, lo], device_id)


def build_set_crosstalk_group(input_id: int, group: int,
                               device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    return _build(CAT_PAD, PAD_SET_XTALK, [input_id, group], device_id)


def build_get_pad_config(input_id: int, device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    return _build(CAT_PAD, PAD_GET, [input_id], device_id)


def build_link_inputs(input_a: int, input_b: int,
                      device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_a)
    _check_input_id(input_b)
    if input_a == input_b:
        raise ValueError("Cannot link an input to itself")
    return _build(CAT_PAD, PAD_LINK, [input_a, input_b], device_id)


def build_unlink_input(input_id: int, device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    return _build(CAT_PAD, PAD_UNLINK, [input_id], device_id)


def build_get_input_status(input_id: int, device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    return _build(CAT_PAD, PAD_GET_STATUS, [input_id], device_id)


def build_set_head_sensitivity(input_id: int, value: int,
                               device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    hi, lo = encode_14bit(value)
    return _build(CAT_PAD, PAD_SET_SENS, [input_id, hi, lo], device_id)


def build_set_scan_time(input_id: int, ms: int,
                        device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    hi, lo = encode_14bit(ms)
    return _build(CAT_PAD, PAD_SET_SCAN, [input_id, hi, lo], device_id)


def build_set_mask_time(input_id: int, ms: int,
                        device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    hi, lo = encode_14bit(ms)
    return _build(CAT_PAD, PAD_SET_MASK, [input_id, hi, lo], device_id)


def build_set_rim_sensitivity(input_id: int, value: int,
                              device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    hi, lo = encode_14bit(value)
    return _build(CAT_PAD, PAD_SET_RIM_SENS, [input_id, hi, lo], device_id)


def build_set_rim_threshold(input_id: int, value: int,
                            device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    hi, lo = encode_14bit(value)
    return _build(CAT_PAD, PAD_SET_RIM_THRESH, [input_id, hi, lo], device_id)


# ---------------------------------------------------------------------------
# Category 03 builders — MIDI mapping
# ---------------------------------------------------------------------------

def build_set_note_mapping(input_id: int, note: int, channel: int,
                            device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    return _build(CAT_MIDI, MIDI_SET_NOTE, [input_id, note, channel], device_id)


def build_set_zone2_mapping(input_id: int, note: int, channel: int,
                             device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    return _build(CAT_MIDI, MIDI_SET_Z2, [input_id, note, channel], device_id)


def build_set_cc_mapping(input_id: int, cc_number: int, channel: int,
                          device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    return _build(CAT_MIDI, MIDI_SET_CC, [input_id, cc_number, channel], device_id)


def build_get_midi_mapping(input_id: int, device_id: int = DEV_HEAD) -> bytearray:
    _check_input_id(input_id)
    return _build(CAT_MIDI, MIDI_GET, [input_id], device_id)


# ---------------------------------------------------------------------------
# Category 04 builders — Preset management
# ---------------------------------------------------------------------------

def build_load_preset(preset_id: int, device_id: int = DEV_HEAD) -> bytearray:
    _check_preset_id(preset_id)
    return _build(CAT_PRESET, PRE_LOAD, [preset_id], device_id)


def build_save_preset(preset_id: int, name: str,
                      device_id: int = DEV_HEAD) -> bytearray:
    _check_preset_id(preset_id)
    name_bytes = name.encode("ascii")
    if len(name_bytes) > PRESET_NAME_MAX:
        raise ValueError(f"Preset name too long (max {PRESET_NAME_MAX} chars)")
    payload = [preset_id, len(name_bytes), *name_bytes]
    return _build(CAT_PRESET, PRE_SAVE, payload, device_id)


def build_list_presets(device_id: int = DEV_HEAD) -> bytearray:
    return _build(CAT_PRESET, PRE_LIST, device_id=device_id)


def build_delete_preset(preset_id: int, device_id: int = DEV_HEAD) -> bytearray:
    _check_preset_id(preset_id)
    return _build(CAT_PRESET, PRE_DELETE, [preset_id], device_id)


def build_export_preset(preset_id: int, device_id: int = DEV_HEAD) -> bytearray:
    _check_preset_id(preset_id)
    return _build(CAT_PRESET, PRE_EXPORT, [preset_id], device_id)


# ---------------------------------------------------------------------------
# Response parsers  (device -> app)
# ---------------------------------------------------------------------------

def parse_identify_response(payload: bytes) -> dict:
    """01 04 -> {fw_maj, fw_min, device_id, num_inputs}"""
    _require_len(payload, 4, "identify_response")
    return {
        "fw_maj":     payload[0],
        "fw_min":     payload[1],
        "device_id":  payload[2],
        "num_inputs": payload[3],
    }


def parse_sys_ack(payload: bytes) -> dict:
    """01 07 -> {status, status_name}"""
    _require_len(payload, 1, "sys_ack")
    status = payload[0]
    return {
        "status":      status,
        "status_name": ACK_STATUS_NAMES.get(status, f"0x{status:02X}"),
    }


def parse_pad_config_response(payload: bytes) -> dict:
    """
    02 07 -> {input_id, pad_type, pad_type_name, threshold,
             velocity_curve, curve_name, retrigger_time, crosstalk_group,
             head_sensitivity, scan_time, mask_time,
             rim_sensitivity, rim_threshold}
    """
    _require_len(payload, 18, "pad_config_response")
    return {
        "input_id":         payload[0],
        "pad_type":         payload[1],
        "pad_type_name":    PAD_TYPE_NAMES.get(payload[1], f"0x{payload[1]:02X}"),
        "threshold":        decode_14bit(payload[2], payload[3]),
        "velocity_curve":   payload[4],
        "curve_name":       CURVE_NAMES.get(payload[4], f"0x{payload[4]:02X}"),
        "retrigger_time":   decode_14bit(payload[5], payload[6]),
        "crosstalk_group":  payload[7],
        "head_sensitivity": decode_14bit(payload[8],  payload[9]),
        "scan_time":        decode_14bit(payload[10], payload[11]),
        "mask_time":        decode_14bit(payload[12], payload[13]),
        "rim_sensitivity":  decode_14bit(payload[14], payload[15]),
        "rim_threshold":    decode_14bit(payload[16], payload[17]),
    }


def parse_input_status_response(payload: bytes) -> dict:
    """02 0A -> {input_id, status, status_name}"""
    _require_len(payload, 2, "input_status_response")
    status = payload[1]
    return {
        "input_id":    payload[0],
        "status":      status,
        "status_name": INPUT_STATUS_NAMES.get(status, f"0x{status:02X}"),
    }


def parse_midi_mapping_response(payload: bytes) -> dict:
    """03 05 -> {input_id, midi_note, midi_channel, zone2_note, zone2_channel, cc_number, cc_channel}"""
    _require_len(payload, 7, "midi_mapping_response")
    return {
        "input_id":     payload[0],
        "midi_note":    payload[1],
        "midi_channel": payload[2],
        "zone2_note":   payload[3],
        "zone2_channel": payload[4],
        "cc_number":    payload[5],
        "cc_channel":   payload[6],
    }


def parse_list_presets_response(payload: bytes) -> dict:
    """04 04 -> {count, presets: [{id, name}, ...]}"""
    _require_len(payload, 1, "list_presets_response")
    count = payload[0]
    presets = []
    pos = 1
    for _ in range(count):
        if pos + 2 > len(payload):
            raise ValueError("list_presets_response: truncated entry")
        preset_id = payload[pos]
        name_len  = payload[pos + 1]
        pos += 2
        if pos + name_len > len(payload):
            raise ValueError("list_presets_response: truncated name")
        name = payload[pos : pos + name_len].decode("ascii", errors="replace")
        pos += name_len
        presets.append({"id": preset_id, "name": name})
    return {"count": count, "presets": presets}


def _parse_input_record(data: bytes, offset: int) -> dict:
    """
    Parse the 24-byte per-input record used by the export command.
    Layout:
      PAD_TYPE
      THRESH_HI THRESH_LO
      CURVE
      RETRIG_HI RETRIG_LO
      XTALK
      SENS_HI SENS_LO
      SCAN_HI SCAN_LO
      MASK_HI MASK_LO
      RIM_SENS_HI RIM_SENS_LO
      RIM_THRESH_HI RIM_THRESH_LO
      MIDI_NOTE MIDI_CH Z2_NOTE Z2_CH CC_NUM CC_CH
      LINKED (0x7F = none)
    """
    if offset + 24 > len(data):
        raise ValueError(f"input record at offset {offset}: truncated")
    d = data[offset:]
    linked_raw = d[23]
    return {
        "pad_type":         d[0],
        "pad_type_name":    PAD_TYPE_NAMES.get(d[0], f"0x{d[0]:02X}"),
        "threshold":        decode_14bit(d[1], d[2]),
        "velocity_curve":   d[3],
        "curve_name":       CURVE_NAMES.get(d[3], f"0x{d[3]:02X}"),
        "retrigger_time":   decode_14bit(d[4], d[5]),
        "crosstalk_group":  d[6],
        "head_sensitivity": decode_14bit(d[7],  d[8]),
        "scan_time":        decode_14bit(d[9],  d[10]),
        "mask_time":        decode_14bit(d[11], d[12]),
        "rim_sensitivity":  decode_14bit(d[13], d[14]),
        "rim_threshold":    decode_14bit(d[15], d[16]),
        "midi_note":        d[17],
        "midi_channel":     d[18],
        "zone2_note":       d[19],
        "zone2_channel":    d[20],
        "cc_number":        d[21],
        "cc_channel":       d[22],
        "linked_input":     None if linked_raw == LINKED_NONE else linked_raw,
    }


def parse_export_preset_response(payload: bytes) -> dict:
    """
    04 06 -> {preset_id, name, inputs: [9 × input dict]}

    Each input dict is the 14-byte per-input record emitted by SysEx.cpp's
    SYSEX_PRE_EXPORT handler; field names match parse_pad_config_response
    plus the midi/cc/linked fields from parse_midi_mapping_response.
    """
    _require_len(payload, 2, "export_preset_response")
    preset_id = payload[0]
    name_len  = payload[1]
    _require_len(payload, 2 + name_len + NUM_INPUTS * 24, "export_preset_response")
    name   = payload[2 : 2 + name_len].decode("ascii", errors="replace")
    offset = 2 + name_len
    inputs = [_parse_input_record(payload, offset + i * 24) for i in range(NUM_INPUTS)]
    return {"preset_id": preset_id, "name": name, "inputs": inputs}


def parse_command_ack(payload: bytes) -> dict:
    """05 01 -> {cmd_high, cmd_low, status, status_name}"""
    _require_len(payload, 3, "command_ack")
    status = payload[2]
    return {
        "cmd_high":    payload[0],
        "cmd_low":     payload[1],
        "status":      status,
        "status_name": ACK_STATUS_NAMES.get(status, f"0x{status:02X}"),
    }


def parse_input_error(payload: bytes) -> dict:
    """05 02 -> {input_id, error_code}"""
    _require_len(payload, 2, "input_error")
    return {"input_id": payload[0], "error_code": payload[1]}


def parse_hit_event(payload: bytes) -> dict:
    """
    05 03 -> {input_id, zone, zone_name, raw_velocity, midi_velocity}

    raw_velocity:  pre-curve sensor reading mapped to 0-127
    midi_velocity: post-curve MIDI output velocity
    """
    _require_len(payload, 4, "hit_event")
    zone = payload[1]
    return {
        "input_id":      payload[0],
        "zone":          zone,
        "zone_name":     ZONE_NAMES.get(zone, f"0x{zone:02X}"),
        "raw_velocity":  payload[2],
        "midi_velocity": payload[3],
    }


# ---------------------------------------------------------------------------
# __main__ — round-trip self-test
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    import sys

    _PASS = "\033[32mPASS\033[0m"
    _FAIL = "\033[31mFAIL\033[0m"
    _errors = 0

    def _check(label: str, condition: bool) -> None:
        global _errors
        tag = _PASS if condition else _FAIL
        if not condition:
            _errors += 1
        print(f"  {tag}  {label}")

    print("=== eDrum SysEx round-trip self-test ===\n")

    # ── 7-bit encoding ───────────────────────────────────────────────────────
    print("7-bit encoding:")
    hi, lo = encode_14bit(1000)
    _check("encode_14bit(1000) -> (0x07, 0x68)", (hi, lo) == (0x07, 0x68))
    _check("decode_14bit round-trip 1000",       decode_14bit(hi, lo) == 1000)
    _check("encode/decode 0",                    decode_14bit(*encode_14bit(0))     == 0)
    _check("encode/decode 16383",                decode_14bit(*encode_14bit(16383)) == 16383)
    _check("encode/decode 512",                  decode_14bit(*encode_14bit(512))   == 512)

    # ── Ping ─────────────────────────────────────────────────────────────────
    print("\nPing (01 01):")
    msg = build_ping()
    print(f"  bytes: {' '.join(f'{b:02X}' for b in msg)}")
    parsed = parse_message(msg)
    _check("parse_message returns dict",  parsed is not None)
    _check("device_id == DEV_HEAD",       parsed["device_id"] == DEV_HEAD)
    _check("cmd_high  == CAT_SYS",        parsed["cmd_high"]  == CAT_SYS)
    _check("cmd_low   == SYS_PING",       parsed["cmd_low"]   == SYS_PING)
    _check("payload is empty",            parsed["payload"]   == b"")

    # ── Identify request ─────────────────────────────────────────────────────
    print("\nIdentify request (01 03):")
    msg = build_identify_request()
    parsed = parse_message(msg)
    _check("cmd_low == SYS_IDENT_REQ", parsed["cmd_low"] == SYS_IDENT_REQ)

    # ── Identify response parser ──────────────────────────────────────────────
    print("\nIdentify response parser (01 04):")
    result = parse_identify_response(bytes([0, 1, DEV_HEAD, NUM_INPUTS]))
    _check("fw_maj == 0",            result["fw_maj"]     == 0)
    _check("fw_min == 1",            result["fw_min"]     == 1)
    _check("device_id == DEV_HEAD",  result["device_id"]  == DEV_HEAD)
    _check("num_inputs == 9",        result["num_inputs"] == NUM_INPUTS)

    # ── Set threshold (14-bit encoding in message) ────────────────────────────
    print("\nSet threshold (02 02) — input 2, value 1000:")
    msg = build_set_threshold(2, 1000)
    print(f"  bytes: {' '.join(f'{b:02X}' for b in msg)}")
    parsed = parse_message(msg)
    p = parsed["payload"]
    _check("cmd_high == CAT_PAD",         parsed["cmd_high"] == CAT_PAD)
    _check("cmd_low  == PAD_SET_THRESH",  parsed["cmd_low"]  == PAD_SET_THRESH)
    _check("input_id == 2",               p[0] == 2)
    _check("14-bit round-trips to 1000",  decode_14bit(p[1], p[2]) == 1000)

    # ── Set retrigger time ────────────────────────────────────────────────────
    print("\nSet retrigger time (02 04) — input 0, 50 ms:")
    msg = build_set_retrigger_time(0, 50)
    p = parse_message(msg)["payload"]
    _check("input_id == 0",              p[0] == 0)
    _check("14-bit round-trips to 50",   decode_14bit(p[1], p[2]) == 50)

    # ── Pad config response parser ────────────────────────────────────────────
    print("\nPad config response parser (02 07) — 18 bytes:")
    thi, tlo = encode_14bit(750)
    rhi, rlo = encode_14bit(50)
    shi, slo = encode_14bit(1000)
    sch, scl = encode_14bit(10)
    mhi, mlo = encode_14bit(30)
    rshi, rslo = encode_14bit(200)
    rthi, rtlo = encode_14bit(30)
    fake = bytes([
        3, PAD_TYPE_PIEZO_RIM, thi, tlo, CURVE_EXPRESSIVE, rhi, rlo, 1,
        shi, slo, sch, scl, mhi, mlo, rshi, rslo, rthi, rtlo
    ])
    result = parse_pad_config_response(fake)
    _check("input_id == 3",              result["input_id"]         == 3)
    _check("pad_type == PIEZO_RIM",      result["pad_type"]         == PAD_TYPE_PIEZO_RIM)
    _check("threshold == 750",           result["threshold"]        == 750)
    _check("curve == EXPRESSIVE",        result["velocity_curve"]   == CURVE_EXPRESSIVE)
    _check("curve_name == 'Expressive'", result["curve_name"]       == "Expressive")
    _check("retrigger_time == 50",       result["retrigger_time"]   == 50)
    _check("crosstalk_group == 1",       result["crosstalk_group"]  == 1)
    _check("head_sensitivity == 1000",   result["head_sensitivity"] == 1000)
    _check("scan_time == 10",            result["scan_time"]        == 10)
    _check("mask_time == 30",            result["mask_time"]        == 30)
    _check("rim_sensitivity == 200",     result["rim_sensitivity"]  == 200)
    _check("rim_threshold == 30",        result["rim_threshold"]    == 30)

    # ── Link / unlink ─────────────────────────────────────────────────────────
    print("\nLink inputs (02 08):")
    msg = build_link_inputs(0, 1)
    p = parse_message(msg)["payload"]
    _check("input_a == 0",  p[0] == 0)
    _check("input_b == 1",  p[1] == 1)

    # ── MIDI mapping round-trip ───────────────────────────────────────────────
    print("\nMIDI mapping builders (03 xx):")
    msg = build_set_note_mapping(0, 36, 1)
    p = parse_message(msg)["payload"]
    _check("set_note: cmd == MIDI_SET_NOTE", parse_message(msg)["cmd_low"] == MIDI_SET_NOTE)
    _check("set_note: note == 36",           p[1] == 36)
    _check("set_note: channel == 1",         p[2] == 1)

    msg = build_set_zone2_mapping(1, 38, 1)
    _check("set_z2: cmd == MIDI_SET_Z2", parse_message(msg)["cmd_low"] == MIDI_SET_Z2)

    msg = build_set_cc_mapping(4, 4, 1)
    _check("set_cc: cmd == MIDI_SET_CC", parse_message(msg)["cmd_low"] == MIDI_SET_CC)

    # ── MIDI mapping response parser ──────────────────────────────────────────
    print("\nMIDI mapping response parser (03 05):")
    fake = bytes([2, 38, 1, 40, 1, 4, 1])
    result = parse_midi_mapping_response(fake)
    _check("input_id == 2",     result["input_id"]      == 2)
    _check("midi_note == 38",   result["midi_note"]      == 38)
    _check("zone2_note == 40",  result["zone2_note"]     == 40)
    _check("cc_number == 4",    result["cc_number"]      == 4)

    # ── Save preset builder ───────────────────────────────────────────────────
    print("\nSave preset (04 02):")
    msg = build_save_preset(0, "Kit A")
    p = parse_message(msg)["payload"]
    _check("preset_id == 0",     p[0] == 0)
    _check("name_len == 5",      p[1] == 5)
    _check("name bytes correct", bytes(p[2:7]) == b"Kit A")

    # ── List presets response parser ──────────────────────────────────────────
    print("\nList presets response parser (04 04):")
    fake = bytes([2, 0x00, 5]) + b"Kit A" + bytes([0x03, 4]) + b"Jazz"
    result = parse_list_presets_response(fake)
    _check("count == 2",                 result["count"] == 2)
    _check("preset[0].id == 0",          result["presets"][0]["id"]   == 0)
    _check("preset[0].name == 'Kit A'",  result["presets"][0]["name"] == "Kit A")
    _check("preset[1].id == 3",          result["presets"][1]["id"]   == 3)
    _check("preset[1].name == 'Jazz'",   result["presets"][1]["name"] == "Jazz")

    # ── Command ack parser ────────────────────────────────────────────────────
    print("\nCommand ack parser (05 01):")
    result = parse_command_ack(bytes([CAT_PAD, PAD_SET_THRESH, ACK_OK]))
    _check("cmd_high == CAT_PAD",         result["cmd_high"]    == CAT_PAD)
    _check("cmd_low  == PAD_SET_THRESH",  result["cmd_low"]     == PAD_SET_THRESH)
    _check("status == ACK_OK",            result["status"]      == ACK_OK)
    _check("status_name == 'ok'",         result["status_name"] == "ok")

    result = parse_command_ack(bytes([CAT_SYS, SYS_RESET, ACK_ERROR]))
    _check("error status_name correct",   result["status_name"] == "error")

    # ── Hit event parser ──────────────────────────────────────────────────────
    print("\nHit event parser (05 03):")
    result = parse_hit_event(bytes([5, ZONE_HEAD, 80, 100]))
    _check("input_id == 5",           result["input_id"]      == 5)
    _check("zone == ZONE_HEAD",       result["zone"]           == ZONE_HEAD)
    _check("zone_name == 'head'",     result["zone_name"]      == "head")
    _check("raw_velocity == 80",      result["raw_velocity"]   == 80)
    _check("midi_velocity == 100",    result["midi_velocity"]  == 100)

    result = parse_hit_event(bytes([3, ZONE_RIM, 50, 64]))
    _check("rim: input_id == 3",      result["input_id"]      == 3)
    _check("rim: zone == RIM",        result["zone"]           == ZONE_RIM)
    _check("rim: raw_velocity == 50", result["raw_velocity"]   == 50)
    _check("rim: midi_vel == 64",     result["midi_velocity"]  == 64)

    # ── Validation errors ─────────────────────────────────────────────────────
    print("\nValidation errors:")
    try:
        build_set_threshold(9, 100)
        _check("input_id 9 -> ValueError", False)
    except ValueError:
        _check("input_id 9 -> ValueError", True)

    try:
        build_link_inputs(3, 3)
        _check("link input to itself -> ValueError", False)
    except ValueError:
        _check("link input to itself -> ValueError", True)

    try:
        encode_14bit(16384)
        _check("encode_14bit(16384) -> ValueError", False)
    except ValueError:
        _check("encode_14bit(16384) -> ValueError", True)

    try:
        build_save_preset(0, "A" * 17)
        _check("name > 16 chars -> ValueError", False)
    except ValueError:
        _check("name > 16 chars -> ValueError", True)

    _check("truncated message (6 bytes) -> None",
           parse_message(bytes([0xF0, 0x00, 0x7D, 0x00, 0x01, 0x01])) is None)
    _check("wrong manufacturer ID -> None",
           parse_message(bytes([0xF0, 0x41, 0x7D, 0x00, 0x01, 0x01, 0xF7])) is None)
    _check("missing F0 -> None",
           parse_message(bytes([0x00, 0x00, 0x7D, 0x00, 0x01, 0x01, 0xF7])) is None)

    # ── Summary ───────────────────────────────────────────────────────────────
    print()
    if _errors == 0:
        print("All tests passed.")
    else:
        print(f"{_errors} test(s) FAILED.")
    sys.exit(0 if _errors == 0 else 1)
