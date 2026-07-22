import serial
import time
import sys

port = '/dev/ttyACM0'
try:
    s = serial.Serial(port, 115200, timeout=1)
    print("Triggering touch on the first book...")
    s.write(b"T 50 100\n")
    time.sleep(0.5)
    
    print("Listening for crash logs...")
    s.timeout = 5
    start = time.time()
    while time.time() - start < 10:
        line = s.readline()
        if line:
            print(line.decode('utf-8', 'ignore').strip())
            
    s.close()
except Exception as e:
    print(e)
