#!/bin/bash
# Retries flashing the app image until the USB link answers.
end=$((SECONDS+${2:-200}))
while [ $SECONDS -lt $end ]; do
  if python3 ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 --before default_reset --after hard_reset write_flash 0x10000 ${1:-.pio/build/t5-47-s3/firmware.bin} > flashloop.log 2>&1; then
    echo "flashed at $SECONDS"; tail -3 flashloop.log; exit 0
  fi
  sleep 0.2
done
echo "gave up"; tail -5 flashloop.log; exit 1
