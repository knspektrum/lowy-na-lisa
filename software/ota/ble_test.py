"""Talks to a LisekOta device the way the web page does, for testing from a PC.

    python3 ble_test.py info   <name>
    python3 ble_test.py flash  <name> <file.lsk> [--no-confirm]
    python3 ble_test.py set    <name> <key=value>
    python3 ble_test.py confirm <name>

Needs bleak (nix-shell -p 'python3.withPackages(p:[p.bleak])').
"""
import asyncio
import json
import sys
import time

from bleak import BleakClient, BleakScanner

SVC = "4c49534b-0001-4f54-8000-00805f9b34fb"
INFO = "4c49534b-0002-4f54-8000-00805f9b34fb"
CTRL = "4c49534b-0003-4f54-8000-00805f9b34fb"
DATA = "4c49534b-0004-4f54-8000-00805f9b34fb"


async def find(name, timeout=30):
    dev = await BleakScanner.find_device_by_name(name, timeout=timeout)
    if not dev:
        sys.exit(f"{name} not found")
    return dev


class Dev:
    def __init__(self, client):
        self.c = client
        self.notes = asyncio.Queue()

    async def start(self):
        await self.c.start_notify(CTRL, lambda _, v: self.notes.put_nowait(bytes(v).decode()))

    async def info(self):
        return json.loads(await self.c.read_gatt_char(INFO))

    async def cmd(self, cmd, want, timeout=10):
        await self.c.write_gatt_char(CTRL, cmd.encode(), response=True)
        return await self.wait(want, timeout)

    async def wait(self, want, timeout):
        end = time.time() + timeout
        while True:
            n = await asyncio.wait_for(self.notes.get(), max(0.1, end - time.time()))
            if n.startswith("error:"):
                raise RuntimeError(n)
            if n.startswith(want):
                return n


async def connect(name, tries=10):
    # right after a reboot the device can refuse the first GATT requests
    for i in range(tries):
        try:
            return await connect_once(name)
        except Exception as e:
            if i == tries - 1:
                raise
            print("connect:", type(e).__name__, e)
            await asyncio.sleep(3)


async def connect_once(name):
    c = BleakClient(await find(name))
    await c.connect()
    try:
        await c._backend._acquire_mtu()
    except Exception:
        pass
    d = Dev(c)
    await d.start()
    return d


async def main():
    op, name = sys.argv[1], sys.argv[2]
    d = await connect(name)
    print("before:", await d.info(), "host mtu", d.c.mtu_size)
    if op == "confirm":
        print(await d.cmd("confirm", "confirmed"))
        print("after:", await d.info())
    elif op == "set":
        print(await d.cmd("set " + sys.argv[3], "set-ok"))
        print("after:", await d.info())
    elif op == "flash":
        blob = open(sys.argv[3], "rb").read()
        print(await d.cmd("begin", "ready"))
        chunk = min(512, d.c.mtu_size - 3)
        t0 = time.time()
        acked, done = 0, False
        try:
            # Writes without response; the device acks every 8 KiB ("progress:N/T",
            # N = image bytes), at most WINDOW bytes may be unacknowledged.
            WINDOW, HDR = 24 * 1024, 168
            # firmware before the fast path only takes writes with response
            fast = "write-without-response" in d.c.services.get_characteristic(DATA).properties
            for o in range(0, len(blob), chunk):
                while (fast and o - HDR - acked > WINDOW) or not d.notes.empty():
                    n = await asyncio.wait_for(d.notes.get(), 20)
                    if n.startswith("error:"):
                        raise RuntimeError(n)
                    if n.startswith("progress:"):
                        acked = int(n.split(":")[1].split("/")[0])
                        if acked % (256 * 1024) < 8192:
                            print(n, f"{acked / (time.time() - t0) / 1024:.1f} KiB/s")
                    if n.startswith("done"):
                        done = True
                await d.c.write_gatt_char(DATA, blob[o:o + chunk], response=not fast)
            if not done:
                await d.wait("done", 30)
            print("done", f"in {time.time() - t0:.0f} s, {len(blob) / (time.time() - t0) / 1024:.1f} KiB/s")
        except RuntimeError as e:
            print("REJECTED:", e)
            await d.c.disconnect()
            return
        await asyncio.sleep(4)
        d = await connect(name)
        info = await d.info()
        print("after reboot:", info)
        if "--no-confirm" in sys.argv:
            print("not confirming; the device should roll back within 5 minutes")
        else:
            for _ in range(30):
                try:
                    print(await d.cmd("confirm", "confirmed"))
                    break
                except RuntimeError as e:
                    print("confirm:", e)
                    await asyncio.sleep(3)
            print("final:", await d.info())
    await d.c.disconnect()


asyncio.run(main())
