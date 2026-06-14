#!/bin/bash
# Convenience wrapper around idf.py and espota.
# Usage:
#   ./build.sh build
#   ./build.sh flash                    (USB, requires device connected)
#   ./build.sh ota [IP]                 (OTA via espota, default 192.168.1.209)
#   ./build.sh flash monitor            (USB flash then open serial monitor)
#   ./build.sh monitor

OTA_IP="${2:-192.168.1.209}"
ESPOTA=~/.platformio/packages/framework-arduinoespressif32/tools/espota.py

if [ "$1" = "ota" ]; then
    shift
    echo "OTA upload to ${OTA_IP}..."
    python3 "$ESPOTA" -i "$OTA_IP" -p 3232 -f build/withrottle_knob.bin
    exit $?
fi

IDF_PATH=~/.platformio/packages/framework-espidf@src-c44434145e05010467d5d5a727b42ef9 \
PATH=~/.platformio/packages/toolchain-xtensa-esp-elf/bin:~/.platformio/packages/tool-cmake/bin:~/.platformio/packages/tool-ninja:~/.espressif/idf_venv/bin:$PATH \
IDF_PYTHON_ENV_PATH=~/.espressif/idf_venv \
ESP_IDF_VERSION=5.3.2 \
~/.espressif/idf_venv/bin/python \
  ~/.platformio/packages/framework-espidf@src-c44434145e05010467d5d5a727b42ef9/tools/idf.py "$@"
