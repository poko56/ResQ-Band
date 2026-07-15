import serial
import time
import sys

port = "/dev/cu.usbserial-0001"
baud = 115200

try:
    ser = serial.Serial(port, baud, timeout=1)
    ser.setDTR(False)
    ser.setRTS(False)
    
    print(f"Connected to {port}. Waiting for logs... (Please press EN/RST button on the board now!)")
    start_time = time.time()
    while time.time() - start_time < 30:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='replace')
            print(line, end='')
    ser.close()
except Exception as e:
    print(f"Error: {e}")
