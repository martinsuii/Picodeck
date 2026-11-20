#!/usr/bin/env bash
# Build one custom app folder into <folder>/app.bin
#
#   ./build.sh examples/hello
#
# The folder may contain *.c, *.cpp and an app.ini. Copy the whole folder to
# /apps on the PicoPad SD card afterwards.
set -e

here="$(cd "$(dirname "$0")" && pwd)"
appdir="${1:-}"
if [ -z "$appdir" ] || [ ! -d "$appdir" ]; then
    echo "usage: $0 <app-folder>"
    exit 1
fi
appdir="$(cd "$appdir" && pwd)"

csrc=$(find "$appdir" -maxdepth 1 -name '*.c' | sort | tr '\n' ' ')
cppsrc=$(find "$appdir" -maxdepth 1 -name '*.cpp' | sort | tr '\n' ' ')

if [ -z "$csrc$cppsrc" ]; then
    echo "error: no .c or .cpp files in $appdir"
    exit 1
fi

export PICO_ROOT_PATH="${PICO_ROOT_PATH:-$here/../picolibsdk}"
export EXTRA_CFLAGS="${EXTRA_CFLAGS:-} -flto"

rm -rf "$here/build"
mkdir -p "$here/build"
make -C "$here" -j"$(nproc)" elf bin siz \
    TARGET=app \
    APP_CSRC="$csrc" \
    APP_SRC="$cppsrc" \
    PICO_ROOT_PATH="$PICO_ROOT_PATH" \
    DEVICE=picopad10 \
    DEVCLASS=picopad \
    EXTRA_CFLAGS="$EXTRA_CFLAGS"

cp "$here/app.bin" "$appdir/app.bin"
echo
echo "Built: $appdir/app.bin"
arm-none-eabi-size "$here/build/app.elf" 2>/dev/null || true
