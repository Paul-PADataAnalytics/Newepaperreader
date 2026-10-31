import serial, time
end=time.time()+130
while time.time()<end:
    try:
        s=serial.Serial()
        s.port='/dev/ttyACM0'; s.baudrate=115200
        s.dtr=False; s.rts=True
        s.open()
        time.sleep(0.1); s.rts=False; time.sleep(0.1)
        s.close()
        print("reset ok at", time.strftime("%H:%M:%S")); break
    except Exception as e:
        time.sleep(0.05)
