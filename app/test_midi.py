import rtmidi
import time

mid = rtmidi.MidiIn()
ports = mid.get_ports()
idx = next((i for i, n in enumerate(ports) if 'edrum' in n.lower()), None)

mid.open_port(idx)
mid.ignore_types(sysex=False, timing=True, active_sense=True)

print("Polling for 10 seconds...")
start = time.time()
while time.time() - start < 10:
    msg = mid.get_message()
    if msg:
        print("MSG:", [hex(b) for b in msg[0]])
    time.sleep(0.001)

mid.close_port()