#!/usr/bin/env python3
import sys
import threading
import time
import os
import glob

try:
    import serial
except ImportError:
    print("Error: pyserial is not installed.")
    print("Please install it by running: pip install pyserial")
    sys.exit(1)

BAUD = 115200
FB_SIZE = 960 * 540 // 2


def resolve_port():
    env_port = os.environ.get("EPD_SERIAL_PORT")
    if env_port:
        return env_port

    candidates = sorted(glob.glob('/dev/ttyACM*'))
    if candidates:
        return candidates[0]

    candidates = sorted(glob.glob('/dev/ttyUSB*'))
    if candidates:
        return candidates[0]

    return '/dev/ttyACM0'


PORT = resolve_port()

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
except Exception as e:
    print(f"Failed to open {PORT}: {e}")
    sys.exit(1)

screenshot_mode = False

def read_from_port():
    global screenshot_mode
    while True:
        if screenshot_mode:
            time.sleep(0.01)
            continue
            
        try:
            line = ser.readline()
            if line:
                decoded = line.decode('utf-8', errors='ignore').strip()
                print(f"[DEVICE] {decoded}")
        except Exception as e:
            print(f"Read error: {e}")
            break

def take_screenshot():
    global screenshot_mode
    screenshot_mode = True # Stop background thread from reading
    
    ser.write(b"S\n")
    print("Requested screenshot. Waiting for SCREENSHOT_START...")
    
    # Wait for the device to signal start
    start_time = time.time()
    while True:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line == "SCREENSHOT_START":
            break
        elif line:
             print(f"[DEVICE] {line}")
        
        if time.time() - start_time > 5:
            print("Timeout waiting for screenshot start.")
            screenshot_mode = False
            return

    print("Receiving screenshot data (approx 20 seconds at 115200 baud)...")
    
    # Read the PGM header
    header = b""
    while not header.endswith(b"255\n") and not header.endswith(b"255\r\n"):
        header += ser.read(1)
        if time.time() - start_time > 10 and len(header) < 10:
             print("Timeout waiting for header.")
             screenshot_mode = False
             return
    
    # Read the exact framebuffer size
    data = b""
    while len(data) < FB_SIZE:
        chunk = ser.read(FB_SIZE - len(data))
        if chunk:
            data += chunk
            
    # Save the file
    filename = f"screenshot_{int(time.time())}.pgm"
    with open(filename, "wb") as f:
        f.write(header)
        f.write(data)
        
    print(f"\nScreenshot saved to {filename} ({len(data)} bytes)")
    
    # Wait for SCREENSHOT_END
    end = ser.readline().decode('utf-8', errors='ignore').strip()
    if "SCREENSHOT_END" in end:
         print("Screenshot transfer completed successfully.")
         
    screenshot_mode = False


import struct

def save_as_bmp(filename, width, height, data_4bit):
    pixels = bytearray(width * height)
    for i in range(len(data_4bit)):
        b = data_4bit[i]
        p1 = b & 0x0F
        p2 = (b >> 4) & 0x0F
        pixels[i*2] = p1 * 17
        pixels[i*2+1] = p2 * 17
        
    file_size = 14 + 40 + 1024 + len(pixels)
    offset = 14 + 40 + 1024
    
    with open(filename, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<I", file_size))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", offset))
        f.write(struct.pack("<I", 40))
        f.write(struct.pack("<i", width))
        f.write(struct.pack("<i", -height))
        f.write(struct.pack("<H", 1))
        f.write(struct.pack("<H", 8))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<I", len(pixels)))
        f.write(struct.pack("<I", 2835))
        f.write(struct.pack("<I", 2835))
        f.write(struct.pack("<I", 256))
        f.write(struct.pack("<I", 256))
        for i in range(256):
            f.write(struct.pack("<BBBB", i, i, i, 0))
        f.write(pixels)

def main():
    print(f"Connected to {PORT} at {BAUD} baud.")
    print("Commands:")
    print("  touch <x> <y>   - Simulate a touch event at x,y")
    print("  screenshot      - Download the current screen buffer to a .bmp file")
    print("  exit            - Quit")
    
    # We will use non-blocking IO for stdin
    import select
    
    while True:
        try:
            # Check for serial data
            while ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(f"[DEVICE] {line}")
            
            # Check for user input
            i, o, e = select.select([sys.stdin], [], [], 0.1)
            if i:
                cmd = sys.stdin.readline().strip()
                if not cmd:
                    continue
                    
                if cmd == "exit":
                    break
                elif cmd == "screenshot":
                    ser.write(b"S\n")
                    print("Requested screenshot...")
                    
                    header = b""
                    start = time.time()
                    while not header.endswith(b"255\n") and not header.endswith(b"255\r\n"):
                        if ser.in_waiting:
                            header += ser.read(1)
                        if time.time() - start > 10:
                            print("Timeout waiting for header.")
                            break
                    
                    if header.endswith(b"255\n") or header.endswith(b"255\r\n"):
                        print("Receiving screenshot data...")
                        data = b""
                        while len(data) < FB_SIZE:
                            chunk = ser.read(FB_SIZE - len(data))
                            if chunk:
                                data += chunk
                        
                        filename = f"screenshot_{int(time.time())}.bmp"
                        save_as_bmp(filename, 960, 540, data)
                        print(f"\nScreenshot saved to {filename}")
                elif cmd.startswith("touch "):
                    parts = cmd.split()
                    if len(parts) == 3:
                        ser.write(f"T {parts[1]} {parts[2]}\n".encode())
                        print(f"Injected touch at {parts[1]}, {parts[2]}")
                else:
                    print("Unknown command.")
                    
        except KeyboardInterrupt:
            break

    ser.close()
    print("Disconnected.")

if __name__ == "__main__":
    main()
