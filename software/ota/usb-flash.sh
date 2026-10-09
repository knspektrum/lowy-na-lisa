#!/usr/bin/env bash
# Flashes the firmware built by build.sh over USB, to any number of boards at
# once (one cable per board):
#   software/ota/usb-flash.sh receiver /dev/ttyUSB0 /dev/ttyUSB1
#   software/ota/usb-flash.sh transmitter /dev/ttyUSB2
# Needed once per board: it writes the partition table that Bluetooth updates rely on.
set -euo pipefail
cd "$(dirname "$0")"
role=$1
shift
case $role in
  receiver) sketch=led-receiver fqbn=esp32:esp32:esp32 opts=(--board-options UploadSpeed=115200) ;;
  transmitter) sketch=fox-transmitter fqbn=esp32:esp32:d1_uno32 opts=() ;;
  *) echo "usage: $0 receiver|transmitter <port>..." >&2; exit 2 ;;
esac
[ -f "build/$role/$sketch.ino.bin" ] || { echo "run build.sh first" >&2; exit 1; }
[ $# -gt 0 ] || { echo "no ports given" >&2; exit 2; }
pids=()
for port in "$@"; do
  log=build/flash-$(basename "$port").log
  nix run nixpkgs#arduino-cli -- upload --fqbn "$fqbn" "${opts[@]}" -p "$port" --input-dir "build/$role" \
    > "$log" 2>&1 &
  pids+=("$!:$port:$log")
done
failed=0
for p in "${pids[@]}"; do
  IFS=: read -r pid port log <<< "$p"
  if wait "$pid"; then echo "$port: ok"; else echo "$port: FAILED (see software/ota/$log)"; failed=1; fi
done
exit $failed
