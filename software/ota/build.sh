#!/usr/bin/env bash
# Builds the receiver and transmitter firmware and signs both for the web
# flasher: software/web-flasher/firmware/{receiver,transmitter}.lsk plus
# manifest.json. Plain USB images stay in software/ota/build/<role>/.
# Partition table: min_spiffs (two 1.9 MB app slots). It is only written over
# USB, so every board gets its first flash of this firmware by cable.
set -euo pipefail
cd "$(dirname "$0")"
sw=$(cd .. && pwd)
# Short enough to fit the BLE advertisement: commit, "+" when built from uncommitted changes.
version=$(git rev-parse --short=7 HEAD)$(git diff --quiet HEAD -- .. 2>/dev/null || echo +)
out=$sw/web-flasher/firmware
mkdir -p "$out" build
py="nix-shell -p python3.withPackages(p:[p.cryptography]) --run"

build() {  # role sketch fqbn
  nix run nixpkgs#arduino-cli -- compile --fqbn "$3" --libraries "$sw/libraries" \
    --build-property "compiler.cpp.extra_flags=-DLISEK_VERSION=\"$version\"" \
    --build-property build.partitions=min_spiffs --build-property upload.maximum_size=1966080 \
    --output-dir "build/$1" "$sw/$2" > "build/$1.log" 2>&1 || { cat "build/$1.log"; exit 1; }
  tail -2 "build/$1.log"
  $py "python3 -I sign.py sign $1 $version build/$1/$2.ino.bin $out/$1.lsk"
}
build receiver led-receiver esp32:esp32:esp32
build transmitter fox-transmitter esp32:esp32:d1_uno32
printf '{"receiver":"%s","transmitter":"%s"}\n' "$version" "$version" > "$out/manifest.json"
