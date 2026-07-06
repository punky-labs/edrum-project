#!/usr/bin/env python3
"""
telnet_logger.py — minimal interactive telnet client with file logging, for
standalone eDrum hardware test sessions (pad characterization, tuning runs,
etc.) that don't need a live Claude session in the loop.

Usage:
    python tools/telnet_logger.py [--host 192.168.1.168] [--port 23] [--log logs/session.log]

Type commands and press Enter to send them to the device (e.g. "o 0 15",
"w 0 thresh 20", "d", "s"). All device output is printed to the console AND
appended to the log file with per-line timestamps, so a full test session
can be run solo and the resulting log handed off for analysis afterward.

NOTE: the device's telnet server accepts one client at a time. If Claude's
telnet MCP connection is currently active, disconnect it first (or ask
Claude to disconnect) before running this.

Press Ctrl+C to exit cleanly.
"""
import argparse
import socket
import sys
import threading
import datetime
import os


def reader_thread(sock, log_file):
    buf = b""
    while True:
        try:
            data = sock.recv(4096)
        except OSError:
            break
        if not data:
            print("\n[telnet_logger] Connection closed by device.")
            break
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode(errors="replace").rstrip("\r")
            ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            out = f"[{ts}] {text}"
            print(out)
            log_file.write(out + "\n")
            log_file.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.1.168")
    ap.add_argument("--port", type=int, default=23)
    ap.add_argument("--log", default=None,
                     help="Log file path (default: tools/logs/telnet_<timestamp>.log)")
    args = ap.parse_args()

    log_path = args.log
    if not log_path:
        log_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
        os.makedirs(log_dir, exist_ok=True)
        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        log_path = os.path.join(log_dir, f"telnet_{stamp}.log")

    print(f"[telnet_logger] Connecting to {args.host}:{args.port} ...")
    try:
        sock = socket.create_connection((args.host, args.port), timeout=5)
    except OSError as e:
        print(f"[telnet_logger] Connection failed: {e}")
        print("[telnet_logger] Is the device's telnet already in use by another client?")
        sys.exit(1)
    sock.settimeout(None)
    print(f"[telnet_logger] Connected. Logging to {log_path}")
    print("[telnet_logger] Type commands + Enter to send. Ctrl+C to quit.\n")

    with open(log_path, "a", encoding="utf-8") as log_file:
        log_file.write(f"\n=== Session started {datetime.datetime.now().isoformat()} "
                        f"({args.host}:{args.port}) ===\n")
        t = threading.Thread(target=reader_thread, args=(sock, log_file), daemon=True)
        t.start()
        try:
            while True:
                line = sys.stdin.readline()
                if not line:
                    break
                sock.sendall(line.encode())
        except KeyboardInterrupt:
            pass
        finally:
            log_file.write(f"=== Session ended {datetime.datetime.now().isoformat()} ===\n")
            sock.close()
            print(f"\n[telnet_logger] Session closed. Log saved: {log_path}")


if __name__ == "__main__":
    main()
