#!/usr/bin/env python3
"""
Drives the native simulator (./run-native.sh) over its TCP companion port.

The simulator speaks the same companion protocol as the BLE firmware (see
docs/companion_protocol.md), framed as <u16 little-endian length><payload>,
plus a few simulator-only test hooks.

Examples:
    tools/sim_client.py tap 270 480            # tap at on-screen coords
    tools/sim_client.py shot /tmp/home.png     # screenshot (portrait PNG)
    tools/sim_client.py hello
    tools/sim_client.py library
    tools/sim_client.py upload path/to/book.epub
    tools/sim_client.py delete /books/book.epub
"""
import json
import os
import socket
import struct
import sys
import time

HOST, PORT = "127.0.0.1", int(os.environ.get("EPD_SIM_PORT", "9876"))


class Sim:
    def __init__(self):
        self.sock = socket.create_connection((HOST, PORT), timeout=10)
        self.buf = b""
        self.rid = 0
        time.sleep(0.2)

    def send(self, payload):
        if isinstance(payload, (dict, list)):
            payload = json.dumps(payload).encode()
        self.sock.sendall(struct.pack("<H", len(payload)) + payload)

    def recv(self, timeout=10.0):
        self.sock.settimeout(timeout)
        while True:
            if len(self.buf) >= 2:
                n = struct.unpack("<H", self.buf[:2])[0]
                if len(self.buf) >= 2 + n:
                    frame, self.buf = self.buf[2:2 + n], self.buf[2 + n:]
                    return json.loads(frame.decode())
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("simulator closed the connection")
            self.buf += chunk

    def request(self, msg, timeout=20.0):
        self.rid += 1
        msg = dict(msg, rid=self.rid)
        self.send(msg)
        deadline = time.time() + timeout
        while time.time() < deadline:
            r = self.recv(deadline - time.time())
            if r.get("rid") == self.rid:
                return r
        raise TimeoutError("no response to %s" % msg["t"])

    def paged(self, msg):
        items, off = [], 0
        while off >= 0:
            r = self.request(dict(msg, off=off))
            items += r.get("items", [])
            off = r.get("next", -1)
        return items

    def cmd(self, msg):
        self.send(msg)
        return self.recv()


def shot(sim, out):
    pgm = os.path.abspath(out + ".pgm")
    sim.cmd({"cmd": "dump", "path": pgm})
    time.sleep(0.1)
    from PIL import Image
    Image.open(pgm).save(out)
    os.remove(pgm)
    print("saved", out)


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    sim = Sim()
    op = argv[0]
    if op == "tap":
        sim.cmd({"cmd": "touch", "x": int(argv[1]), "y": int(argv[2])})
        time.sleep(float(argv[3]) if len(argv) > 3 else 0.6)
    elif op == "shot":
        shot(sim, argv[1])
    elif op == "hello":
        print(sim.request({"t": "HELLO"}))
    elif op == "library":
        for b in sim.paged({"t": "GET_LIBRARY"}):
            print(b)
    elif op == "bookmarks":
        for b in sim.paged({"t": "GET_BOOKMARKS"}):
            print(b)
    elif op == "delete":
        print(sim.request({"t": "DELETE_FILE", "path": argv[1]}))
    elif op == "upload":
        path = argv[1]
        name = argv[2] if len(argv) > 2 else os.path.basename(path)
        data = open(path, "rb").read()
        r = sim.request({"t": "FILE_BEGIN", "name": name, "size": len(data)})
        print(r)
        if not r.get("ok"):
            return 1
        chunk = 495
        for off in range(0, len(data), chunk):
            sim.send(bytes([1]) + struct.pack("<I", off) + data[off:off + chunk])
        print(sim.request({"t": "FILE_END", "size": len(data)}, timeout=60))
    else:
        print("unknown op", op)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
