"""
eDrum emulator device — holds in-memory device state and responds to SysEx.

Mirrors the firmware's SysEx command handling so the Python app can be
exercised without physical hardware.
"""
from __future__ import annotations

import json
import logging
import os
import threading
from typing import Optional

log = logging.getLogger("edrum.emulator.device")

try:
    from ..protocol.sysex import (
        DEV_HEAD, NUM_INPUTS,
        CAT_SYS, CAT_PAD, CAT_MIDI, CAT_STATUS,
        SYS_PING, SYS_PONG, SYS_IDENT_REQ, SYS_IDENT_RESP,
        SYS_SAVE, SYS_RESET,
        PAD_GET, PAD_RESP, PAD_GET_STATUS,
        PAD_SET_TYPE, PAD_SET_THRESH, PAD_SET_CURVE, PAD_SET_RETRIG,
        PAD_SET_XTALK, PAD_SET_SENS, PAD_SET_SCAN, PAD_SET_MASK,
        PAD_SET_RIM_SENS, PAD_SET_RIM_THRESH,
        PAD_LINK, PAD_UNLINK,
        MIDI_GET, MIDI_RESP,
        MIDI_SET_NOTE, MIDI_SET_Z2, MIDI_SET_CC,
        STAT_ACK, ACK_OK, ACK_ERROR, ACK_UNKNOWN,
        INPUT_ACTIVE,
        build_message, encode_14bit, decode_14bit,
    )
except ImportError:
    from protocol.sysex import (  # type: ignore[no-redef]
        DEV_HEAD, NUM_INPUTS,
        CAT_SYS, CAT_PAD, CAT_MIDI, CAT_STATUS,
        SYS_PING, SYS_PONG, SYS_IDENT_REQ, SYS_IDENT_RESP,
        SYS_SAVE, SYS_RESET,
        PAD_GET, PAD_RESP, PAD_GET_STATUS,
        PAD_SET_TYPE, PAD_SET_THRESH, PAD_SET_CURVE, PAD_SET_RETRIG,
        PAD_SET_XTALK, PAD_SET_SENS, PAD_SET_SCAN, PAD_SET_MASK,
        PAD_SET_RIM_SENS, PAD_SET_RIM_THRESH,
        PAD_LINK, PAD_UNLINK,
        MIDI_GET, MIDI_RESP,
        MIDI_SET_NOTE, MIDI_SET_Z2, MIDI_SET_CC,
        STAT_ACK, ACK_OK, ACK_ERROR, ACK_UNKNOWN,
        INPUT_ACTIVE,
        build_message, encode_14bit, decode_14bit,
    )

_APP_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
_DEFAULT_STATE_PATH = os.path.join(_APP_DIR, "emulator_state.json")


def _default_input(input_id: int) -> dict:
    return {
        "pad_type":         0,
        "threshold":        512,
        "velocity_curve":   0,
        "retrigger_time":   25,
        "crosstalk_group":  0,
        "head_sensitivity": 512,
        "scan_time":        1,
        "mask_time":        10,
        "rim_sensitivity":  256,
        "rim_threshold":    128,
        "midi_note":        36 + input_id,
        "midi_channel":     1,
        "zone2_note":       min(36 + input_id + 1, 127),
        "zone2_channel":    1,
        "cc_number":        4,
        "cc_channel":       1,
        "linked_input":     None,
    }


class EmulatorDevice:
    """
    In-memory emulation of the eDrum head unit firmware.

    Processes parsed SysEx commands and returns response messages using
    the same protocol as the real device. State persists to a JSON file.
    """

    def __init__(self, state_path: Optional[str] = None) -> None:
        self._state_path = state_path or _DEFAULT_STATE_PATH
        self._inputs: dict[int, dict] = {}
        self._lock = threading.Lock()
        self._load_or_default()

    # ------------------------------------------------------------------
    # State persistence
    # ------------------------------------------------------------------

    def _load_or_default(self) -> None:
        if os.path.exists(self._state_path):
            try:
                with open(self._state_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                for i in range(NUM_INPUTS):
                    base = _default_input(i)
                    saved = data.get(str(i), {})
                    base.update(saved)
                    self._inputs[i] = base
                log.debug("Loaded emulator state from %s", self._state_path)
                return
            except Exception as exc:
                log.warning("Failed to load state from %s: %s; using defaults",
                            self._state_path, exc)
        self._reset_to_defaults()

    def _reset_to_defaults(self) -> None:
        for i in range(NUM_INPUTS):
            self._inputs[i] = _default_input(i)
        log.debug("Emulator state reset to defaults")

    def _save(self) -> None:
        data = {str(k): v for k, v in self._inputs.items()}
        try:
            parent = os.path.dirname(os.path.abspath(self._state_path))
            os.makedirs(parent, exist_ok=True)
            with open(self._state_path, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
            log.debug("Saved emulator state to %s", self._state_path)
        except Exception as exc:
            log.warning("Failed to save state to %s: %s", self._state_path, exc)

    # ------------------------------------------------------------------
    # Response builders
    # ------------------------------------------------------------------

    def _ack(self, cmd_high: int, cmd_low: int,
             status: int = ACK_OK) -> bytearray:
        return build_message(DEV_HEAD, CAT_STATUS, STAT_ACK,
                             [cmd_high, cmd_low, status])

    def _build_pad_resp(self, input_id: int) -> bytearray:
        cfg = self._inputs[input_id]
        thi,  tlo  = encode_14bit(cfg["threshold"])
        rhi,  rlo  = encode_14bit(cfg["retrigger_time"])
        shi,  slo  = encode_14bit(cfg["head_sensitivity"])
        sch,  scl  = encode_14bit(cfg["scan_time"])
        mhi,  mlo  = encode_14bit(cfg["mask_time"])
        rshi, rslo = encode_14bit(cfg["rim_sensitivity"])
        rthi, rtlo = encode_14bit(cfg["rim_threshold"])
        payload = [
            input_id,
            cfg["pad_type"],
            thi, tlo,
            cfg["velocity_curve"],
            rhi, rlo,
            cfg["crosstalk_group"],
            shi, slo,
            sch, scl,
            mhi, mlo,
            rshi, rslo,
            rthi, rtlo,
        ]
        return build_message(DEV_HEAD, CAT_PAD, PAD_RESP, payload)

    def _build_midi_resp(self, input_id: int) -> bytearray:
        cfg = self._inputs[input_id]
        payload = [
            input_id,
            cfg["midi_note"],
            cfg["midi_channel"],
            cfg["zone2_note"],
            cfg["zone2_channel"],
            cfg["cc_number"],
            cfg["cc_channel"],
        ]
        return build_message(DEV_HEAD, CAT_MIDI, MIDI_RESP, payload)

    # ------------------------------------------------------------------
    # Command dispatch
    # ------------------------------------------------------------------

    def handle(self, parsed: Optional[dict]) -> list[bytearray]:
        """Process a parsed SysEx message and return response messages."""
        if not parsed:
            return []

        hi  = parsed.get("cmd_high", 0)
        lo  = parsed.get("cmd_low",  0)
        pay = bytes(parsed.get("payload", b""))

        log.debug("RX cmd=%02X %02X payload=%s", hi, lo,
                  pay.hex(" ").upper() if pay else "(empty)")

        with self._lock:
            if hi == CAT_SYS:
                responses = self._handle_sys(lo, pay)
            elif hi == CAT_PAD:
                responses = self._handle_pad(lo, pay)
            elif hi == CAT_MIDI:
                responses = self._handle_midi(lo, pay)
            else:
                responses = [self._ack(hi, lo, ACK_UNKNOWN)]

        for r in responses:
            log.debug("TX: %s", r.hex(" ").upper())

        return responses

    # ------------------------------------------------------------------
    # Category handlers
    # ------------------------------------------------------------------

    def _handle_sys(self, lo: int, pay: bytes) -> list[bytearray]:
        if lo == SYS_PING:
            return [build_message(DEV_HEAD, CAT_SYS, SYS_PONG)]
        elif lo == SYS_IDENT_REQ:
            return [build_message(DEV_HEAD, CAT_SYS, SYS_IDENT_RESP,
                                  [0, 2, 0, 9])]
        elif lo == SYS_SAVE:
            self._save()
            return [self._ack(CAT_SYS, SYS_SAVE)]
        elif lo == SYS_RESET:
            self._reset_to_defaults()
            return [self._ack(CAT_SYS, SYS_RESET)]
        return [self._ack(CAT_SYS, lo, ACK_UNKNOWN)]

    def _handle_pad(self, lo: int, pay: bytes) -> list[bytearray]:
        if not pay:
            return [self._ack(CAT_PAD, lo, ACK_UNKNOWN)]

        input_id = pay[0]
        if input_id not in self._inputs:
            return [self._ack(CAT_PAD, lo, ACK_ERROR)]

        if lo == PAD_GET:
            return [self._build_pad_resp(input_id)]

        elif lo == PAD_GET_STATUS:
            return [build_message(DEV_HEAD, CAT_PAD, PAD_GET_STATUS,
                                  [input_id, INPUT_ACTIVE])]

        elif lo == PAD_SET_TYPE:
            if len(pay) < 2:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            self._inputs[input_id]["pad_type"] = pay[1]

        elif lo == PAD_SET_THRESH:
            if len(pay) < 3:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            self._inputs[input_id]["threshold"] = decode_14bit(pay[1], pay[2])

        elif lo == PAD_SET_CURVE:
            if len(pay) < 2:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            self._inputs[input_id]["velocity_curve"] = pay[1]

        elif lo == PAD_SET_RETRIG:
            if len(pay) < 3:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            self._inputs[input_id]["retrigger_time"] = decode_14bit(pay[1], pay[2])

        elif lo == PAD_SET_XTALK:
            if len(pay) < 2:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            self._inputs[input_id]["crosstalk_group"] = pay[1]

        elif lo == PAD_SET_SENS:
            if len(pay) < 3:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            self._inputs[input_id]["head_sensitivity"] = decode_14bit(pay[1], pay[2])

        elif lo == PAD_SET_SCAN:
            if len(pay) < 3:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            self._inputs[input_id]["scan_time"] = decode_14bit(pay[1], pay[2])

        elif lo == PAD_SET_MASK:
            if len(pay) < 3:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            self._inputs[input_id]["mask_time"] = decode_14bit(pay[1], pay[2])

        elif lo == PAD_SET_RIM_SENS:
            if len(pay) < 3:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            self._inputs[input_id]["rim_sensitivity"] = decode_14bit(pay[1], pay[2])

        elif lo == PAD_SET_RIM_THRESH:
            if len(pay) < 3:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            self._inputs[input_id]["rim_threshold"] = decode_14bit(pay[1], pay[2])

        elif lo == PAD_LINK:
            if len(pay) < 2:
                return [self._ack(CAT_PAD, lo, ACK_ERROR)]
            input_a, input_b = pay[0], pay[1]
            if input_a in self._inputs and input_b in self._inputs:
                self._inputs[input_a]["linked_input"] = input_b
                self._inputs[input_b]["linked_input"] = input_a

        elif lo == PAD_UNLINK:
            linked = self._inputs[input_id].get("linked_input")
            self._inputs[input_id]["linked_input"] = None
            if linked is not None and linked in self._inputs:
                self._inputs[linked]["linked_input"] = None

        else:
            return [self._ack(CAT_PAD, lo, ACK_UNKNOWN)]

        return [self._ack(CAT_PAD, lo)]

    def _handle_midi(self, lo: int, pay: bytes) -> list[bytearray]:
        if not pay:
            return [self._ack(CAT_MIDI, lo, ACK_UNKNOWN)]

        input_id = pay[0]
        if input_id not in self._inputs:
            return [self._ack(CAT_MIDI, lo, ACK_ERROR)]

        if lo == MIDI_GET:
            return [self._build_midi_resp(input_id)]

        elif lo == MIDI_SET_NOTE:
            if len(pay) < 3:
                return [self._ack(CAT_MIDI, lo, ACK_ERROR)]
            self._inputs[input_id]["midi_note"]    = pay[1]
            self._inputs[input_id]["midi_channel"] = pay[2]

        elif lo == MIDI_SET_Z2:
            if len(pay) < 3:
                return [self._ack(CAT_MIDI, lo, ACK_ERROR)]
            self._inputs[input_id]["zone2_note"]    = pay[1]
            self._inputs[input_id]["zone2_channel"] = pay[2]

        elif lo == MIDI_SET_CC:
            if len(pay) < 3:
                return [self._ack(CAT_MIDI, lo, ACK_ERROR)]
            self._inputs[input_id]["cc_number"]  = pay[1]
            self._inputs[input_id]["cc_channel"] = pay[2]

        else:
            return [self._ack(CAT_MIDI, lo, ACK_UNKNOWN)]

        return [self._ack(CAT_MIDI, lo)]
