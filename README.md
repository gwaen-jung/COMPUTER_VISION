# UVC Camera Test on WeAct ESP32-S3

Standalone ESP-IDF project to test a USB UVC Camera and stream via Wi-Fi.

## Hardware Setup
- **MCU**: WeAct ESP32-S3 N16R8 (16 MB Flash, 8 MB Octal PSRAM)
- **UART Port (CH343)**: Used for flashing, serial logs, and board power.
- **Native USB Port (GPIO19=D-, GPIO20=D+)**: Used as USB Host for the UVC Camera.
- **Power**: ESP32-S3 does *not* output 5V on VBUS. The camera must be powered by an external 5V source with a shared ground to the ESP32.

## How to use
1. Copy `secrets.example` to `secrets.h` and configure your Wi-Fi.
2. Build and flash via UART port: `pio run -t upload`
3. Connect the camera to the Native USB port.
4. Monitor serial logs to find the IP address.
5. Open `http://<ip-address>/` to view the stream.
