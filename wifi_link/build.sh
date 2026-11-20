#!/usr/bin/env bash
# Build the WiFi Link app with the Raspberry Pi Pico SDK.
#
#   ./build.sh
#
# Flashes like a game: copy the UF2 to the SD card and use LOAD on the PicoPad.
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
cp "$here/build/wifi_link.uf2" "$here/dist/PICODECK_wifi_1.2.5.uf2"
arm-none-eabi-size "$here/build/wifi_link.elf"
echo "UF2: $here/dist/PICODECK_wifi_1.2.5.uf2"
