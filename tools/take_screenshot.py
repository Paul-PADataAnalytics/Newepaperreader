import serial
import time
import sys

def take_screenshot(port):
    with serial.Serial(port, 115200, timeout=30) as ser:
        print("Flushing input...")
        ser.reset_input_buffer()
        print("Sending S command...")
        ser.write(b'S\n')
        
        # Wait for SCREENSHOT_START
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            print("Received:", line)
            if line == "SCREENSHOT_START":
                break
        
        p5 = ser.readline().decode('utf-8').strip()
        dims = ser.readline().decode('utf-8').strip()
        maxval = ser.readline().decode('utf-8').strip()
        
        width, height = map(int, dims.split())
        expected_bytes = (width * height) // 2
        print(f"Reading {expected_bytes} bytes of framebuffer...")
        
        data = bytearray()
        while len(data) < expected_bytes:
            chunk = ser.read(expected_bytes - len(data))
            if not chunk:
                break
            data.extend(chunk)
            
        print(f"Read {len(data)} bytes.")
        print(f"Data sample: {list(data[:20])}")
        
        unpacked = bytearray(width * height)
        # LilyGO packed pixels (often reversed nibbles depending on endianness, try p1 = upper, p2 = lower)
        for i in range(len(data)):
            b = data[i]
            p1 = (b >> 4) * 17
            p2 = (b & 0x0F) * 17
            unpacked[2*i] = p2 # typically lower nibble is first pixel
            unpacked[2*i + 1] = p1
            
        print(f"Unpacked sample: {list(unpacked[:20])}")
        print(f"Min value: {min(unpacked)}, Max value: {max(unpacked)}")
        
        from PIL import Image
        img = Image.frombytes("L", (width, height), bytes(unpacked))
        img.save("screen.png")
        print("Saved screen.png")
        
if __name__ == "__main__":
    take_screenshot("/dev/ttyACM0")
