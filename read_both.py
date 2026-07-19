import serial
import threading
import time

def read_port(port_name, prefix):
    try:
        ser = serial.Serial(port_name, 115200, timeout=1)
        while True:
            line = ser.readline()
            if line:
                print(f"[{prefix}] {line.decode('utf-8', 'ignore').strip()}")
    except Exception as e:
        print(f"[{prefix}] Error: {e}")

t1 = threading.Thread(target=read_port, args=('/dev/cu.usbserial-5C3E1710771', 'RESQ'))
t2 = threading.Thread(target=read_port, args=('/dev/cu.usbserial-0001', 'BAND'))
t1.daemon = True
t2.daemon = True
t1.start()
t2.start()

time.sleep(20)
