#!/usr/bin/env bash
# Flashes and registers every ESP32 board plugged in over USB (see usb_autoflash.py).
#   software/ota/usb-autoflash.sh         # new boards only
#   software/ota/usb-autoflash.sh --all   # also the ones already plugged in
set -euo pipefail
cd "$(dirname "$0")"
want=$(git rev-parse --short=7 HEAD)
have=$(sed -n 's/.*"receiver":"\([^"]*\)".*/\1/p' ../web-flasher/firmware/manifest.json 2>/dev/null || true)
if [ "$have" != "$want" ] || [ ! -f build/receiver/led-receiver.ino.bin ] || [ ! -f build/transmitter/fox-transmitter.ino.bin ]; then
  echo "building firmware $want (have: ${have:-none})"
  ./build.sh
fi
exec nix-shell -p 'python3.withPackages(p:[p.pyserial])' --run "python3 -I -u usb_autoflash.py $*"
