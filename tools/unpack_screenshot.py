import sys
from PIL import Image

def unpack(in_file, out_file):
    with open(in_file, "rb") as f:
        header = f.readline()
        f.readline()
        f.readline()
        data = f.read()
        
    pixels = bytearray(960 * 540)
    for i in range(len(data)):
        b = data[i]
        p1 = b & 0x0F
        p2 = (b >> 4) & 0x0F
        pixels[i*2] = p1 * 17
        pixels[i*2+1] = p2 * 17
        
    img = Image.frombytes("L", (960, 540), bytes(pixels))
    img.save(out_file)

if __name__ == "__main__":
    unpack(sys.argv[1], sys.argv[2])
