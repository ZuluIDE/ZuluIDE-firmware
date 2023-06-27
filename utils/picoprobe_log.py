#!/usr/bin/env python3

import sys
import serial
import serial.tools.list_ports

device = '/dev/ttyACM0'
for port in serial.tools.list_ports.comports():
    if port.product and 'Picoprobe' in port.product:
           device = port.device
           break

print("Using port " + device)
port = serial.Serial(device, 1000000, timeout=0.1)

while True:
        sys.stdout.write(port.read_until('\n', size=4096).decode('ascii', 'replace'))
        sys.stdout.flush()
