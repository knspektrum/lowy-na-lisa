"""Watches for ESP32 boards plugged in over USB, flashes each with the right
firmware (software/ota/build/<role>) and registers it in
software/web-flasher/devices.json, which the fleet page shows.

    software/ota/usb-autoflash.sh          # runs this in a nix shell; Ctrl-C to stop

Role of a board, in order of trust:
  1. its MAC in devices.json (registered before)
  2. the USB-serial chip: CP2102 (10c4:ea60) = receiver (ESP32 DevKit),
     CH340 (1a86:7523) = transmitter (WeMos D1 R32)
After flashing, the boot log must show the role's radio module answering,
otherwise the board is reported as FAILED (wrong role, or module not wired).
"""
import datetime
import glob
import json
import os
import re
import subprocess
import sys
import threading
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
REGISTRY = os.path.join(HERE, "../web-flasher/devices.json")
USB_ROLE = {"10c4:ea60": "receiver", "1a86:7523": "transmitter"}
SKETCH = {"receiver": "led-receiver", "transmitter": "fox-transmitter"}
# boot-log line that proves the radio module answered, per role
HEALTHY = {"receiver": "into RX mode", "transmitter": "transmitter_id="}
lock = threading.Lock()


def log(port, msg):
    with lock:
        print(f"{time.strftime('%H:%M:%S')} {os.path.basename(port)}: {msg}", flush=True)


def load_registry():
    try:
        return json.load(open(REGISTRY))
    except FileNotFoundError:
        return []


def save_registry(entry):
    with lock:
        reg = [d for d in load_registry() if d["mac"] != entry["mac"]] + [entry]
        reg.sort(key=lambda d: (d["role"], d["name"]))
        with open(REGISTRY, "w") as f:
            f.write("[\n" + ",\n".join("  " + json.dumps(d, ensure_ascii=False) for d in reg) + "\n]\n")


def usb_id(port):
    out = subprocess.run(["udevadm", "info", "-q", "property", "-n", port], capture_output=True, text=True).stdout
    p = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)
    return f"{p.get('ID_VENDOR_ID', '')}:{p.get('ID_MODEL_ID', '')}"


def read_mac(port):
    out = subprocess.run(["nix", "run", "nixpkgs#esptool", "--", "--port", port, "read-mac"], capture_output=True, text=True, timeout=60).stdout
    m = re.search(r"MAC:\s+([0-9a-f:]{17})", out)
    return m.group(1) if m else None


def boot_log(port, seconds):
    """Resets the board through EN (RTS) and records what it prints."""
    p = serial.Serial()
    p.port, p.baudrate, p.timeout = port, 115200, 0.2
    p.dtr = p.rts = False
    p.open()
    p.rts = True       # EN low
    time.sleep(0.1)
    p.rts = False      # EN high: boot from flash (IO0 stays high)
    end, buf = time.time() + seconds, b""
    while time.time() < end:
        try:
            buf += p.read(4096)
        except serial.SerialException:
            break
    p.close()
    return buf.decode("utf-8", "replace")


def handle(port):
    usb = usb_id(port)
    try:
        mac = read_mac(port)
    except Exception as e:
        return log(port, f"FAILED: no ESP32 answering ({e})")
    if not mac:
        return log(port, "FAILED: no ESP32 answering")
    known = next((d for d in load_registry() if d["mac"] == mac), None)
    role = known["role"] if known else USB_ROLE.get(usb)
    if not role:
        return log(port, f"FAILED: {mac} unknown and USB chip {usb} does not tell the type; register it in devices.json")
    name = f"lisek-{role[0].upper()}-{mac[12:14]}{mac[15:17]}".upper().replace("LISEK", "lisek")
    log(port, f"{name} ({mac}, {'registered' if known else 'new, by USB chip ' + usb}) -> flashing {role}")
    r = subprocess.run([os.path.join(HERE, "usb-flash.sh"), role, port], capture_output=True, text=True)
    if r.returncode != 0:
        return log(port, f"FAILED: flashing ({r.stdout.strip()} {r.stderr.strip()[-300:]})")
    out = boot_log(port, 15)
    version = (re.findall(r"firmware (\S+)", out) or ["?"])[-1]
    if HEALTHY[role] not in out:
        tail = " | ".join(line.strip() for line in out.splitlines()[-4:])
        return log(port, f"FAILED: {name} runs {version} but its {role} radio module did not answer: {tail}")
    save_registry({"mac": mac, "name": name, "role": role, "usb": usb,
                   "registered": (known or {}).get("registered") or datetime.date.today().isoformat()})
    log(port, f"OK {name}: {role} {version}, radio answers, registered")


def main():
    seen = set(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
    if "--all" in sys.argv:  # also boards already plugged in
        seen = set()
    print(f"watching USB; plug boards in (registry: {os.path.relpath(REGISTRY)}). Ctrl-C to stop.", flush=True)
    while True:
        now = set(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))
        for port in sorted(now - seen):
            time.sleep(1)  # let udev settle
            threading.Thread(target=handle, args=(port,), daemon=True).start()
        seen = now
        time.sleep(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
