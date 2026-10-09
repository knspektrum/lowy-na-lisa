#!/usr/bin/env bash
# Rebuilds led-receiver.fzz and exports its breadboard view with Fritzing (headless).
set -euo pipefail
cd "$(dirname "$0")"
fz=$(nix build nixpkgs#fritzing --print-out-paths -o .fritzing)
nix-shell -p 'python3.withPackages(p:[p.svgelements])' --run "python3 -I make_fzz.py $fz/share/fritzing/parts"
tmp=$(mktemp -d)
cp led-receiver.fzz "$tmp/"
QT_QPA_PLATFORM=offscreen "$fz/bin/Fritzing" -svg "$tmp"
cp "$tmp/led-receiver_breadboard.svg" .
python3 -I ../../../fritzing-lib/strip_watermark.py led-receiver_breadboard.svg
nix run nixpkgs#librsvg -- -w 1800 -b white led-receiver_breadboard.svg -o led-receiver_breadboard.png
rm -rf "$tmp" .fritzing
