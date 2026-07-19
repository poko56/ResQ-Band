import serial
import time
try:
    ser = serial.Serial('/dev/cu.usbserial-0001', 115200, timeout=1)
    # reset via DTR/RTS
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setDTR(True)
    ser.setRTS(False)
    time.sleep(0.1)
    print("Listening to band_node (0001)...")
    while True:
        line = ser.readline()
        if line:
            print(line.decode('utf-8', 'ignore').strip())
except Exception as e:
    print(f"Error: {e}")
