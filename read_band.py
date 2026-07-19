import serial
import time
try:
    ser = serial.Serial('/dev/cu.usbserial-0001', 115200, timeout=1)
    print("Listening to band_node...")
    while True:
        line = ser.readline()
        if line:
            print(line.decode('utf-8', 'ignore').strip())
except Exception as e:
    print(f"Error: {e}")
