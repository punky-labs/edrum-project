"""
MIDI transport layer for the eDrum config app.

Uses python-rtmidi to send and receive SysEx over USB MIDI.
All incoming traffic is filtered for valid eDrum SysEx messages;
everything else is silently ignored.
"""

from __future__ import annotations

import logging
import os
import sys
import threading
from typing import Callable, Optional

import rtmidi

log = logging.getLogger("edrum.transport")

# Support both package import and direct-script execution.
try:
    from ..protocol.sysex import (
        SYSEX_START,
        CAT_SYS, SYS_IDENT_RESP,
        build_identify_request,
        parse_message,
        parse_identify_response,
    )
except ImportError:
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "protocol"))
    from sysex import (  # type: ignore[no-redef]
        SYSEX_START,
        CAT_SYS, SYS_IDENT_RESP,
        build_identify_request,
        parse_message,
        parse_identify_response,
    )


class DrumMidiTransport:
    """
    Bidirectional MIDI transport for the eDrum head unit.

    Opens matching input and output ports by name, enables SysEx
    reception (off by default in rtmidi), and dispatches parsed
    eDrum SysEx messages to a registered callback.

    Usage::

        t = DrumMidiTransport()
        t.connect("eDrum")          # substring match, case-insensitive
        t.set_sysex_callback(on_msg)
        t.send(build_ping())
        t.disconnect()

    Or as a context manager::

        with DrumMidiTransport() as t:
            t.connect("eDrum")
            ...
    """

    def __init__(self) -> None:
        self._midi_in:        Optional[rtmidi.MidiIn]  = None
        self._midi_out:       Optional[rtmidi.MidiOut] = None
        self._port_name:      Optional[str] = None
        self._listeners:      dict[str, Callable[[dict], None]] = {}
        self._listeners_lock  = threading.Lock()
        self._poll_thread:    Optional[threading.Thread] = None
        self._poll_stop:      threading.Event = threading.Event()
        self._sent_cache:     list[bytes] = []
        self._sent_lock:      threading.Lock = threading.Lock()

    # ------------------------------------------------------------------
    # Port discovery
    # ------------------------------------------------------------------

    @staticmethod
    def list_ports() -> dict[str, list[str]]:
        """
        Return all available MIDI port names.

        Returns a dict with keys 'inputs' and 'outputs', each holding a
        list of port name strings.  Ports that appear in both lists can
        be opened bidirectionally with connect().
        """
        tmp_in  = rtmidi.MidiIn()
        tmp_out = rtmidi.MidiOut()
        ports = {
            "inputs":  tmp_in.get_ports(),
            "outputs": tmp_out.get_ports(),
        }
        del tmp_in, tmp_out
        return ports

    # ------------------------------------------------------------------
    # Connection lifecycle
    # ------------------------------------------------------------------

    def connect(self, port_name: str) -> None:
        """
        Open the first input and output ports whose names contain
        port_name (case-insensitive substring match).

        Raises ConnectionError if no matching port is found on either side.
        If already connected, disconnects first.
        """
        if self.is_connected():
            self.disconnect()

        log.info("Connecting to port matching '%s'", port_name)
        needle = port_name.lower()

        midi_in  = rtmidi.MidiIn()
        midi_out = rtmidi.MidiOut()

        in_ports  = midi_in.get_ports()
        out_ports = midi_out.get_ports()

        in_idx  = next((i for i, n in enumerate(in_ports)  if needle in n.lower()), None)
        out_idx = next((i for i, n in enumerate(out_ports) if needle in n.lower()), None)

        if in_idx is None:
            del midi_in, midi_out
            log.error("No input port matching '%s'. Available: %s", port_name, in_ports)
            raise ConnectionError(
                f"No MIDI input port matching '{port_name}'. "
                f"Available inputs: {in_ports}"
            )
        if out_idx is None:
            del midi_in, midi_out
            log.error("No output port matching '%s'. Available: %s", port_name, out_ports)
            raise ConnectionError(
                f"No MIDI output port matching '{port_name}'. "
                f"Available outputs: {out_ports}"
            )

        midi_in.open_port(in_idx)
        # SysEx is ignored by default — must be explicitly enabled.
        midi_in.ignore_types(sysex=False, timing=True, active_sense=True)
        # No set_callback — we poll instead (WinMM drops SysEx in callbacks)
        midi_out.open_port(out_idx)
        log.info("Opened port: in='%s' out='%s'", in_ports[in_idx], out_ports[out_idx])

        self._midi_in   = midi_in
        self._midi_out  = midi_out
        self._port_name = in_ports[in_idx]

        self._poll_stop.clear()
        self._poll_thread = threading.Thread(
            target=self._poll_loop, daemon=True, name="midi-poll"
        )
        self._poll_thread.start()
        log.debug("Poll thread started")

    def disconnect(self) -> None:
        """Close both MIDI ports and release resources."""
        log.info("Disconnecting from '%s'", self._port_name)
        self._poll_stop.set()
        if self._poll_thread is not None:
            self._poll_thread.join(timeout=2.0)
            self._poll_thread = None
            log.debug("Poll thread stopped")
        if self._midi_in is not None:
            self._midi_in.close_port()
            self._midi_in = None
        if self._midi_out is not None:
            self._midi_out.close_port()
            self._midi_out = None
        self._port_name = None

    def is_connected(self) -> bool:
        """Return True if both input and output ports are open."""
        return self._midi_in is not None and self._midi_out is not None

    # ------------------------------------------------------------------
    # Sending
    # ------------------------------------------------------------------

    def send(self, message: bytearray) -> None:
        """
        Send a raw SysEx message over the output port.

        Raises RuntimeError if not connected.
        """
        if not self.is_connected():
            raise RuntimeError("Not connected — call connect() first")
        self._midi_out.send_message(list(message))
        self._record_sent(message)
        log.debug("TX: %s", message.hex(" ").upper())

    def _record_sent(self, message: bytearray) -> None:
        with self._sent_lock:
            self._sent_cache.append(bytes(message))
            if len(self._sent_cache) > 32:
                self._sent_cache.pop(0)

    def _is_echo(self, message: bytes) -> bool:
        """Return True if this message is a recent WinMM loopback echo."""
        with self._sent_lock:
            try:
                self._sent_cache.remove(message)
                log.debug("Echo filtered: %s", message.hex(" ").upper())
                return True
            except ValueError:
                return False

    # ------------------------------------------------------------------
    # Receiving
    # ------------------------------------------------------------------

    @property
    def _sysex_callback(self):
        """Compatibility shim — returns None. Use add/remove_listener."""
        return None

    def add_listener(self, name: str, fn: Callable[[dict], None]) -> None:
        """Register a named SysEx listener. Replaces any existing listener
        with the same name."""
        with self._listeners_lock:
            self._listeners[name] = fn

    def remove_listener(self, name: str) -> None:
        """Remove a named listener. No-op if name is not registered."""
        with self._listeners_lock:
            self._listeners.pop(name, None)

    def _poll_loop(self) -> None:
        """
        Background thread: poll rtmidi for incoming messages.
        Used instead of set_callback() because WinMM on Windows silently
        drops SysEx messages in the callback path.
        """
        while not self._poll_stop.is_set():
            if self._midi_in is None:
                break
            try:
                msg = self._midi_in.get_message()
            except Exception as exc:
                log.error("Poll loop exception: %s", exc, exc_info=True)
                break
            if msg is not None:
                self._on_message(msg, None)
            else:
                self._poll_stop.wait(0.001)

    def _on_message(self, message, data) -> None:
        """
        Message handler — called from _poll_loop().

        get_message() returns (byte_list, delta_time); normalise to byte_list.
        Handles two wire formats:

        - byte 0 == 0xF0: raw SysEx (USB MIDI), passed directly to parse_message.
        - byte 0 >= 0x80 (not 0xF0): BLE MIDI packet.  The OS BLE MIDI driver
          prepends a header byte and optional timestamp bytes (all >= 0x80,
          never 0xF0/0xF7).  Strip them, accumulate the clean SysEx body, then
          dispatch when 0xF7 is seen — matching the firmware's _parsePacket logic.
        """
        # get_message() → (byte_list, delta_time); normalise to byte_list
        if isinstance(message[0], (list, bytes, bytearray)):
            byte_list = message[0]
        else:
            byte_list = message[1]

        if not byte_list:
            return

        first = byte_list[0]

        if first == SYSEX_START:
            if self._is_echo(bytes(byte_list)):
                return
            parsed = parse_message(bytes(byte_list))
            if parsed is None:
                log.warning("RX: unparseable SysEx: %s", bytes(byte_list).hex(" ").upper())
                return
            log.debug("RX: %s", bytes(byte_list).hex(" ").upper())
            with self._listeners_lock:
                callbacks = list(self._listeners.values())
            for cb in callbacks:
                try:
                    cb(parsed)
                except Exception:
                    pass

        elif first >= 0x80:
            # BLE MIDI: skip byte 0 (header); bytes >= 0x80 that are not 0xF0/0xF7
            # are timestamp bytes — ignore them.
            buf: bytearray = bytearray()
            in_sysex = False
            for b in byte_list[1:]:
                if b == 0xF0:
                    buf = bytearray([0xF0])
                    in_sysex = True
                elif b == 0xF7:
                    if in_sysex:
                        buf.append(0xF7)
                        parsed = parse_message(bytes(buf))
                        if parsed is not None:
                            with self._listeners_lock:
                                callbacks = list(self._listeners.values())
                            for cb in callbacks:
                                try:
                                    cb(parsed)
                                except Exception:
                                    pass
                    in_sysex = False
                    buf = bytearray()
                elif b < 0x80:
                    if in_sysex:
                        buf.append(b)
                # else: timestamp or other status byte — skip

    # ------------------------------------------------------------------
    # Context manager
    # ------------------------------------------------------------------

    def __enter__(self) -> DrumMidiTransport:
        return self

    def __exit__(self, *_) -> None:
        self.disconnect()

    def __repr__(self) -> str:
        if self.is_connected():
            return f"<DrumMidiTransport connected to '{self._port_name}'>"
        return "<DrumMidiTransport disconnected>"


# ---------------------------------------------------------------------------
# Convenience helpers
# ---------------------------------------------------------------------------

def request_identify(
    transport: DrumMidiTransport,
    timeout: float = 2.0,
) -> dict:
    """
    Send an identify request and block until the device responds.

    Returns the parsed identify response dict::

        {'fw_maj': 0, 'fw_min': 1, 'device_id': 0, 'num_inputs': 9}

    Saves and restores any previously registered SysEx callback so
    this function is safe to call mid-session.

    Raises:
        RuntimeError:  transport is not connected.
        TimeoutError:  no response within `timeout` seconds.
    """
    if not transport.is_connected():
        raise RuntimeError("Transport is not connected")

    event        = threading.Event()
    result: dict = {}

    def _handler(msg: dict) -> None:
        if msg["cmd_high"] == CAT_SYS and msg["cmd_low"] == SYS_IDENT_RESP:
            result.update(parse_identify_response(msg["payload"]))
            event.set()

    transport.add_listener("identify", _handler)
    transport.send(build_identify_request())
    got_response = event.wait(timeout)
    transport.remove_listener("identify")

    if not got_response:
        raise TimeoutError(
            f"No identify response from device within {timeout:.1f}s"
        )
    return result


# ---------------------------------------------------------------------------
# __main__ — port discovery smoke-test
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    ports = DrumMidiTransport.list_ports()

    print(f"python-rtmidi {rtmidi.get_rtmidi_version()}\n")

    print("Input ports:")
    if ports["inputs"]:
        for i, name in enumerate(ports["inputs"]):
            print(f"  [{i}] {name}")
    else:
        print("  (none found)")

    print("\nOutput ports:")
    if ports["outputs"]:
        for i, name in enumerate(ports["outputs"]):
            print(f"  [{i}] {name}")
    else:
        print("  (none found)")

    bidirectional = sorted(
        set(ports["inputs"]) & set(ports["outputs"])
    )
    print("\nBidirectional (usable with DrumMidiTransport.connect()):")
    if bidirectional:
        for name in bidirectional:
            print(f"  {name}")
    else:
        print("  (none -- device not connected, or port names differ between directions)")
