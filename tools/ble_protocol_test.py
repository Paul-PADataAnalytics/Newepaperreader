#!/usr/bin/env python3
"""
End-to-end test of the companion protocol v2 against a real EPD-Reader over
BLE (needs `pip install bleak` and a Bluetooth adapter).

    python3 tools/ble_protocol_test.py                 # hello, library, bookmarks
    python3 tools/ble_protocol_test.py --upload FILE   # also upload, verify, delete
"""
import argparse
import asyncio
import json
import os
import struct
import time

from bleak import BleakClient, BleakScanner

SERVICE = "12345678-1234-1234-1234-123456789abc"
WRITE = "12345678-1234-1234-1234-123456789001"
NOTIFY = "12345678-1234-1234-1234-123456789002"
RESP = "12345678-1234-1234-1234-123456789003"


class Link:
    def __init__(self, client):
        self.c = client
        self.rid = 0
        self.waiters = {}

    def _on_resp(self, _h, data):
        try:
            msg = json.loads(bytes(data).decode())
        except Exception:
            return
        fut = self.waiters.pop(msg.get("rid"), None)
        if fut and not fut.done():
            fut.set_result(msg)

    async def start(self):
        await self.c.start_notify(RESP, self._on_resp)

    async def request(self, msg, timeout=20):
        self.rid += 1
        rid = self.rid
        fut = asyncio.get_event_loop().create_future()
        self.waiters[rid] = fut
        await self.c.write_gatt_char(WRITE, json.dumps(dict(msg, rid=rid)).encode(), response=True)
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                return await asyncio.wait_for(asyncio.shield(fut), 0.5)
            except asyncio.TimeoutError:
                try:
                    r = json.loads(bytes(await self.c.read_gatt_char(RESP)).decode())
                    if r.get("rid") == rid:
                        self.waiters.pop(rid, None)
                        return r
                except Exception:
                    pass
        raise TimeoutError(msg["t"])

    async def paged(self, msg):
        items, off = [], 0
        while off >= 0:
            r = await self.request(dict(msg, off=off), timeout=40)
            items += r.get("items", [])
            off = r.get("next", -1)
        return items


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--upload")
    args = ap.parse_args()

    print("scanning...")
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name == "EPD-Reader" or SERVICE in (ad.service_uuids or []), timeout=15)
    if not dev:
        raise SystemExit("EPD-Reader not found")
    async with BleakClient(dev) as client:
        if hasattr(client, "_backend") and hasattr(client._backend, "_acquire_mtu"):
            try:
                await client._backend._acquire_mtu()  # BlueZ: learn the negotiated MTU
            except Exception:
                pass
        print("connected, mtu", client.mtu_size)
        link = Link(client)
        await link.start()
        print(await link.request({"t": "HELLO"}))
        now = time.time()
        print(await link.request({"t": "TIME", "val": int(now), "tz": int(-time.timezone / 60 + (60 if time.daylight and time.localtime().tm_isdst else 0))}))

        t0 = time.time()
        books = await link.paged({"t": "GET_LIBRARY"})
        print("library: %d books in %.1fs" % (len(books), time.time() - t0))
        for b in books[:5]:
            print("  ", b)
        marks = await link.paged({"t": "GET_BOOKMARKS"})
        print("bookmarks:", len(marks))

        if args.upload:
            data = open(args.upload, "rb").read()
            name = "zz_ble_test_" + os.path.basename(args.upload)
            r = await link.request({"t": "FILE_BEGIN", "name": name, "size": len(data)})
            print(r)
            chunk = max(20, min(495, client.mtu_size - 3 - 5))
            t0 = time.time()
            for off in range(0, len(data), chunk):
                await client.write_gatt_char(WRITE, bytes([1]) + struct.pack("<I", off) + data[off:off + chunk], response=True)
            r = await link.request({"t": "FILE_END", "size": len(data)}, timeout=60)
            dt = time.time() - t0
            print(r, "%.1f KB/s" % (len(data) / 1024 / dt))
            books = await link.paged({"t": "GET_LIBRARY"})
            match = [b for b in books if b["n"] == name]
            print("listed after upload:", match)
            assert match and match[0]["s"] == len(data), "uploaded file missing or wrong size"
            print(await link.request({"t": "DELETE_FILE", "path": match[0]["p"]}))
            books = await link.paged({"t": "GET_LIBRARY"})
            assert not [b for b in books if b["n"] == name], "file still listed after delete"
            print("upload/delete OK")


if __name__ == "__main__":
    asyncio.run(main())
