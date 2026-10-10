import serial
import time

try:
    s = serial.Serial('COM18', 115200, timeout=1)
    print('Listening on COM18...')
    start = time.time()
    while time.time() - start < 60:
        line = s.readline().decode('utf-8', errors='ignore').strip()
        if line and ('ip:' in line.lower() or 'ip ' in line.lower() or 'http' in line.lower() or 'uvc' in line.lower() or 'sta' in line.lower() or 'failed' in line.lower() or 'error' in line.lower() or 'starting' in line.lower() or 'stream opened' in line.lower() or 'start' in line.lower() or 'connected' in line.lower()):
            print('=>', line)
            if 'starting' in line.lower() or 'started' in line.lower():
                pass
except Exception as e:
    print('Error:', e)
