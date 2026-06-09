"""
eDrum emulator transport — drop-in replacement for DrumMidiTransport.

Short-circuits the MIDI layer entirely: send() passes messages directly
to EmulatorDevice and synchronously fires responses back through the
listener registry, with no threads or virtual ports involved.
"""
from __future__ import annotations

import logging
import threading
from typing import Callable, Optional

log = logging.getLogger("edrum.emulator.transport")

try:
    from ..protocol.sysex import parse_message
    from .device import EmulatorDevice
except ImportError:
    from protocol.sysex import parse_message  # type: ignore[no-redef]
    from emulator.device import EmulatorDevice  # type: ignore[no-redef]


class EmulatorTransport:
    """
    Drop-in replacement for DrumMidiTransport that routes all SysEx
    through an in-process EmulatorDevice instead of real MIDI ports.

    The public API mirrors DrumMidiTransport exactly so the rest of the
    app (main_window, pad_config_tab, write_worker) needs no changes.
    """

    def __init__(self) -> None:
        self._device          = EmulatorDevice()
        self._connected       = False
        self._listeners:      dict[str, Callable[[dict], None]] = {}
        self._listeners_lock  = threading.Lock()

    # ------------------------------------------------------------------
    # Port discovery
    # ------------------------------------------------------------------

    @staticmethod
    def list_ports() -> dict[str, list[str]]:
        return {"inputs": ["eDrum Emulator"], "outputs": ["eDrum Emulator"]}

    # ------------------------------------------------------------------
    # Connection lifecycle
    # ------------------------------------------------------------------

    def connect(self, port_name: str) -> None:
        """Accept any port name; set connected state."""
        self._connected = True
        log.info("EmulatorTransport connected (port='%s')", port_name)

    def disconnect(self) -> None:
        self._connected = False
        log.info("EmulatorTransport disconnected")

    def is_connected(self) -> bool:
        return self._connected

    @property
    def _port_name(self) -> Optional[str]:
        return "eDrum Emulator" if self._connected else None

    # ------------------------------------------------------------------
    # Sending
    # ------------------------------------------------------------------

    def send(self, message: bytearray) -> None:
        """
        Pass message to the device and synchronously dispatch each
        response through the listener registry.

        Raises RuntimeError if not connected.
        """
        if not self._connected:
            raise RuntimeError("EmulatorTransport is not connected")

        log.debug("TX: %s", message.hex(" ").upper())

        parsed = parse_message(message)
        if parsed is None:
            log.warning("send(): could not parse outbound message: %s",
                        message.hex(" ").upper())
            return

        responses = self._device.handle(parsed)

        for response in responses:
            resp_parsed = parse_message(response)
            if resp_parsed is None:
                log.warning("Device returned unparseable response: %s",
                            response.hex(" ").upper())
                continue
            log.debug("RX: %s", response.hex(" ").upper())
            self._dispatch_to_listeners(resp_parsed)

    # ------------------------------------------------------------------
    # Listeners
    # ------------------------------------------------------------------

    def add_listener(self, name: str, fn: Callable[[dict], None]) -> None:
        with self._listeners_lock:
            self._listeners[name] = fn

    def remove_listener(self, name: str) -> None:
        with self._listeners_lock:
            self._listeners.pop(name, None)

    def _dispatch_to_listeners(self, parsed: dict) -> None:
        """Dispatch a parsed message to all registered listeners."""
        with self._listeners_lock:
            callbacks = list(self._listeners.values())
        for cb in callbacks:
            try:
                cb(parsed)
            except Exception:
                pass

    # ------------------------------------------------------------------
    # Context manager (mirrors DrumMidiTransport)
    # ------------------------------------------------------------------

    def __enter__(self) -> EmulatorTransport:
        return self

    def __exit__(self, *_) -> None:
        self.disconnect()

    def __repr__(self) -> str:
        state = "connected" if self._connected else "disconnected"
        return f"<EmulatorTransport {state}>"
