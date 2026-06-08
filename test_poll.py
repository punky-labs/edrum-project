"""
Standalone poll test — run with device connected.
Polls for 5 seconds and prints any received messages.
Run: app\venv\Scripts\python.exe test_poll.py
While running, trigger a hit or send an identify response from the firmware.
"""
import rtmidi, time

mid = rtmidi.MidiIn()
ports = mid.get_ports()
print("Available ports:")
for i, n in enumerate(ports):
    print(f"  [{i}] {n}")

idx = next((i for i, n in enumerate(ports) if "edrum" in n.lower()), None)
if idx is None:
    print("\nNo eDrum port found — connect the device and retry.")
else:
    print(f"\neDrum at index {idx}: {ports[idx]}")
    mid.open_port(idx)
    mid.ignore_types(sysex=False, timing=True, active_sense=True)
    print("Polling 5s for messages...")
    start = time.time()
    while time.time() - start < 5:
        msg = mid.get_message()
        if msg:
            byte_list, delta = msg
            print(f"  MSG (+{delta:.3f}s): {[hex(b) for b in byte_list]}")
        time.sleep(0.001)
    mid.close_port()
    print("Done.")
