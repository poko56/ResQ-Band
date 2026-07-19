import serial
import sys
try:
    ser = serial.Serial('/dev/cu.usbserial-5C3E1710771', 115200, timeout=1)
    for _ in range(5):
        line = ser.readline()
        if line:
            print(line.decode('utf-8', 'ignore').strip())
except Exception as e:
    print(e)
