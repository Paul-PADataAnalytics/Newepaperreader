import serial
import time
import sys

port = '/dev/ttyACM0'
try:
    s = serial.Serial(port, 115200)
    print(f"Resetting {port}...")
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    time.sleep(0.1)
    
    print("Listening for boot logs...")
    s.timeout = 2
    logs = s.read(1000)
    print(logs.decode('utf-8', 'ignore'))
    s.close()
except Exception as e:
    print(e)
