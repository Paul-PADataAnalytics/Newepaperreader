#!/usr/bin/env python3
"""
Development console for the device over USB serial.

    tools/device_console.py status            # battery / USB / CPU / idle / heap
    tools/device_console.py tap 270 480       # tap at on-screen (portrait) coords
    tools/device_console.py shot out.png      # screenshot of the framebuffer
    tools/device_console.py sleep             # enter deep-sleep lock mode
    tools/device_console.py log 10            # print the serial log for 10 s
    tools/device_console.py seq "tap 100 470" "wait 3" "shot a.png" "status"
                                              # several steps in ONE session

Note: the port is opened with DTR/RTS asserted (pyserial default); clearing
them resets the ESP32-S3 through its USB-JTAG reset logic.

Port: $EPD_SERIAL_PORT or the first /dev/ttyACM*.
"""
import glob
import os
import sys
import time

import serial


def open_port():
    port = os.environ.get("EPD_SERIAL_PORT") or (sorted(glob.glob("/dev/ttyACM*")) or ["/dev/ttyACM0"])[0]
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.open()
    return ser


def read_for(ser, seconds, stop=None):
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        out += ser.read(4096)
        if stop and stop in out:
            break
    return out


def screenshot(ser, path):
    ser.reset_input_buffer()
    ser.write(b"S\n")
    data = read_for(ser, 20, b"SCREENSHOT_END")
    start = data.find(b"SCREENSHOT_START")
    if start < 0:
        raise RuntimeError("no screenshot received")
    rest = data[start:].split(b"\n", 4)  # START, P5, "W H", 255, payload
    w, h = (int(v) for v in rest[2].split())
    payload = rest[4][: w * h // 2]
    from PIL import Image
    px = bytearray(w * h)
    for i, b in enumerate(payload):
        px[2 * i] = (b & 0x0F) * 17       # hardware: even pixel in the low nibble
        px[2 * i + 1] = (b >> 4) * 17
    Image.frombytes("L", (w, h), bytes(px)).save(path)
    print("saved", path, w, h)


def wait_for_boot(ser):
    read_for(ser, 6, b"advertising")
    time.sleep(1.5)


def run_op(ser, argv):
    op = argv[0]
    if op == "wait":
        time.sleep(float(argv[1]))
        return 0
    if op == "status":
        ser.reset_input_buffer()
        ser.write(b"STATUS\n")
        for line in read_for(ser, 3).decode(errors="ignore").splitlines():
            if line.startswith("STATUS"):
                print(line)
    elif op == "tap":
        ser.write(("TL %d %d\n" % (int(argv[1]), int(argv[2]))).encode())
        time.sleep(float(argv[3]) if len(argv) > 3 else 2.5)
    elif op == "shot":
        screenshot(ser, argv[1])
    elif op == "sleep":
        ser.write(b"SLEEP\n")
        print(read_for(ser, 8).decode(errors="ignore"))
    elif op == "log":
        end = time.time() + float(argv[1]) if len(argv) > 1 else 10
        while time.time() < end:
            chunk = ser.read(4096)
            if chunk:
                sys.stdout.write(chunk.decode(errors="ignore"))
                sys.stdout.flush()
    else:
        print("unknown op", op)
        return 1
    return 0


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    ser = open_port()
    if argv[0] == "log":
        return run_op(ser, argv)
    time.sleep(0.3)
    if argv[0] == "seq":
        for step in argv[1:]:
            run_op(ser, step.split())
        return 0
    return run_op(ser, argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
