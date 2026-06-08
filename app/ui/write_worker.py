from __future__ import annotations

import threading
from dataclasses import dataclass
from typing import Optional

from PyQt6.QtCore import QObject, QThread, pyqtSignal

try:
    from ..protocol.sysex import CAT_STATUS, STAT_ACK, ACK_OK
    from ..transport.midi import DrumMidiTransport
except ImportError:
    from protocol.sysex import CAT_STATUS, STAT_ACK, ACK_OK  # type: ignore[no-redef]
    from transport.midi import DrumMidiTransport              # type: ignore[no-redef]


@dataclass
class WriteCommand:
    input_id: int       # -1 for global commands (e.g. save to flash)
    param: str          # dedup key — newer supersedes older in queue for same (input_id, param)
    message: bytearray  # complete SysEx message ready to send
    ack_hi: int         # expected in ack payload[0] (cmd category echoed by firmware)
    ack_lo: int         # expected in ack payload[1] (cmd sub-command echoed by firmware)


class _WriteSignals(QObject):
    write_ok      = pyqtSignal(int, str)        # input_id, param
    write_failed  = pyqtSignal(int, str, str)   # input_id, param, reason
    queue_drained = pyqtSignal()


class WriteWorker(QThread):
    """
    Background thread that owns a write queue.

    Sends one SysEx set command at a time, waits for the firmware ack (05 01),
    then moves to the next command.  Commands with the same (input_id, param)
    key are deduplicated: if a newer command arrives before the previous one
    is dequeued, it replaces it in-place.
    """

    def __init__(self, transport: DrumMidiTransport) -> None:
        super().__init__()
        self._transport = transport
        self._lock      = threading.Lock()
        self._wake      = threading.Event()
        self._stop      = threading.Event()
        self._order:   list[tuple[int, str]]              = []
        self._pending: dict[tuple[int, str], WriteCommand] = {}
        self.signals = _WriteSignals()

    # ------------------------------------------------------------------
    # Public API (called from main/UI thread)
    # ------------------------------------------------------------------

    def enqueue(self, cmd: WriteCommand) -> None:
        key = (cmd.input_id, cmd.param)
        with self._lock:
            if key not in self._pending:
                self._order.append(key)
            self._pending[key] = cmd
        self._wake.set()

    def stop(self) -> None:
        self._stop.set()
        self._wake.set()

    # ------------------------------------------------------------------
    # Thread body
    # ------------------------------------------------------------------

    def run(self) -> None:
        while not self._stop.is_set():
            self._wake.wait()
            self._wake.clear()
            if self._stop.is_set():
                break
            while True:
                cmd = self._dequeue()
                if cmd is None:
                    self.signals.queue_drained.emit()
                    break
                self._send_and_wait(cmd)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _dequeue(self) -> Optional[WriteCommand]:
        with self._lock:
            if not self._order:
                return None
            key = self._order.pop(0)
            return self._pending.pop(key, None)

    def _send_and_wait(self, cmd: WriteCommand) -> None:
        event   = threading.Event()
        ok_flag = [False]

        def on_ack(msg: dict) -> None:
            pay = msg.get("payload", b"")
            if (msg.get("cmd_high") == CAT_STATUS
                    and msg.get("cmd_low") == STAT_ACK
                    and len(pay) >= 3
                    and pay[0] == cmd.ack_hi
                    and pay[1] == cmd.ack_lo):
                ok_flag[0] = (pay[2] == ACK_OK)
                event.set()

        self._transport.add_listener("write_worker", on_ack)
        self._transport.send(cmd.message)
        event.wait(2.0)
        self._transport.remove_listener("write_worker")

        if event.is_set():
            if ok_flag[0]:
                self.signals.write_ok.emit(cmd.input_id, cmd.param)
            else:
                self.signals.write_failed.emit(cmd.input_id, cmd.param, "device error")
        else:
            self.signals.write_failed.emit(cmd.input_id, cmd.param, "timeout")
