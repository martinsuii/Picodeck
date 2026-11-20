#!/usr/bin/env bash
# Build the native WiFi module (core 1, game flash area).
set -e
here="$(cd "$(dirname "$0")" && pwd)"
export PICO_SDK_PATH="${PICO_SDK_PATH:-/usr/share/pico-sdk}"
export PICO_BOARD="${PICO_BOARD:-pico_w}"
cmake -S "$here" -B "$here/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DPICO_SDK_PATH="$PICO_SDK_PATH" \
    -DPICO_BOARD="$PICO_BOARD"
ninja -C "$here/build"
mkdir -p "$here/dist"
cp "$here/build/wifi_module.uf2" "$here/dist/PICODECK_wifimod_1.2.20.uf2"
arm-none-eabi-size "$here/build/wifi_module.elf"
echo "UF2: $here/dist/PICODECK_wifimod_1.2.20.uf2"
