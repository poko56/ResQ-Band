import serial
import time
import threading
import glob
import sys
import os

def read_from_port(port, prefix):
    try:
        ser = serial.Serial(port, 115200, timeout=0.1)
        ser.setDTR(False)
        ser.setRTS(False)
        print(f"[{prefix}] Connected to {port}")
        while True:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    print(f"[{prefix}] {line}")
                    sys.stdout.flush()
    except Exception as e:
        print(f"[{prefix}] Error reading from {port}: {e}")

def main():
    ports = glob.glob('/dev/cu.usbserial*') + glob.glob('/dev/cu.usbmodem*')
    if not ports:
        print("No ESP32 boards found! Please plug them in via USB.")
        return
        
    print(f"Found {len(ports)} board(s): {ports}")
    
    threads = []
    for i, port in enumerate(ports):
        prefix = f"BOARD-{i+1}"
        t = threading.Thread(target=read_from_port, args=(port, prefix), daemon=True)
        t.start()
        threads.append(t)
        
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nExiting...")

if __name__ == "__main__":
    main()
