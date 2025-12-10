# CoreFS Ultimate - Build Instructions

# 1. Set target (ESP32-C6 for 4MB flash)
idf.py set-target esp32c6

# 2. Configure (optional)
idf.py menuconfig

# 3. Build
idf.py build

# 4. Flash
idf.py -p /dev/ttyUSB0 flash

# 5. Monitor
idf.py -p /dev/ttyUSB0 monitor

# Complete workflow
idf.py -p /dev/ttyUSB0 build flash monitor

# Expected output:
# - Bootloader: ~80 KB
# - App: ~1.2 MB
# - CoreFS partition: 3.9 MB
# - Total: Perfect fit for 4MB flash!
