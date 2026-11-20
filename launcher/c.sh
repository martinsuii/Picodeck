#!/usr/bin/env bash
# Build script for PicoDeck
#   Usage:  ./c.sh [picopad10|picopad20|picopad20riscv|picopadhstx|picopadhstxriscv]
#   Default device when no arg: picopad10 (RP2040 PicoPad)

set -e

# Default to picopad10 if no argument
DEVICE="${1:-picopad10}"

# Sanity check: PICO_ROOT_PATH must be set
if [ -z "$PICO_ROOT_PATH" ]; then
    echo "ERROR: PICO_ROOT_PATH not set."
    echo "Run: source ../setup_env.sh"
    exit 1
fi

# PicoLibSDK build conventions
export TARGET="PICODECK"
export GRPDIR="PICODECK"
export MEMMAP=""

# Enable LTO (link-time optimization) by default. Massively reduces UF2 size
# (= ~13 KB / 12% on dev build) by stripping cross-unit dead code (e.g.
# FontBold8x16 + variant SelFont* helpers we never call).
export EXTRA_CFLAGS="${EXTRA_CFLAGS:-} -flto"

# Flash layout: 1 MB (default, CIRCUITPY-safe) or 2 MB (= -DLAUNCHER_2MB=1
# in EXTRA_CFLAGS).
#   1 MB: launcher 0x100E0000..0x100FAFFF, recovery 0x100FB000..0x100FDFFF,
#         settings 0x100FE000, CF2 0x100FF000.
#   2 MB: launcher 0x101E0000..0x101FAFFF, recovery 0x101FB000..0x101FDFFF,
#         settings 0x101FE000, CF2 0x101FF000.
# boot2 points at the recovery image, which chain-loads the launcher.
case "${EXTRA_CFLAGS:-}" in
    *-DLAUNCHER_2MB=1*)
        LAUNCHER_BASE_ADDR="0x101E0000"
        RECOVERY_BASE_ADDR="0x101FB000"
        LAYOUT_LABEL="2 MB"
        ;;
    *)
        LAUNCHER_BASE_ADDR="0x100E0000"
        RECOVERY_BASE_ADDR="0x100FB000"
        LAYOUT_LABEL="1 MB"
        ;;
esac
RECOVERY_SIZE_LIMIT=$((0x3000))   # 12 KB recovery partition
echo "Layout: $LAYOUT_LABEL — launcher at $LAUNCHER_BASE_ADDR, recovery at $RECOVERY_BASE_ADDR"

# Extract version from src/main.h so the update UF2 is versioned.
VERSION=$(sed -n 's/.*LAUNCHER_VERSION_STR[[:space:]]*"\([^"]*\)".*/\1/p' src/main.h | head -1)
[ -z "$VERSION" ] && VERSION="unknown"
echo "Version: $VERSION"

# Invoke PicoLibSDK's _c1.sh. The script always exits non-zero (=
# PicoPadLoaderCrc step fails on our custom layout), so plain `|| true` masks
# real compile errors. Capture output, check for actual compile/link errors
# before proceeding.
build_log=$("$PICO_ROOT_PATH/_c1.sh" "$DEVICE" 2>&1) || true
echo "$build_log"

# Real compilation errors trip GCC's "error:" or "fatal error:" output.
# Linker errors typically have "undefined reference" or "ld returned".
if echo "$build_log" | grep -qE 'error:|fatal error|undefined reference|ld returned [1-9]'; then
    echo "ERROR: compilation/link failed — see build log above"
    rm -f PICODECK.bin  # avoid stale cache fooling next build
    exit 1
fi

# Verify the bin was produced
if [ ! -f "PICODECK.bin" ]; then
    echo "ERROR: PICODECK.bin not produced"
    exit 1
fi

# Post-process M1 binary — boot2 now points at the recovery image.
echo
echo "=== M1 post-processing ==="
python ./post_process_boot2.py PICODECK.bin --target "$RECOVERY_BASE_ADDR"

# Build the standalone recovery image. It lives in its own partition and is
# what boot2 actually starts; it chain-loads the launcher on a normal boot.
echo
echo "=== Recovery image ==="
( cd recovery && ./c.sh "$DEVICE" )

if [ ! -f "recovery/RECOVERY.bin" ]; then
    echo "ERROR: recovery/RECOVERY.bin not produced"
    exit 1
fi

RECOVERY_SIZE=$(stat -c%s recovery/RECOVERY.bin)
if [ "$RECOVERY_SIZE" -gt "$RECOVERY_SIZE_LIMIT" ]; then
    echo "ERROR: recovery image ($RECOVERY_SIZE B) exceeds the 12 KB partition"
    exit 1
fi

# "install" UF2 — boot2 + launcher + recovery. For initial install on a blank
# PicoPad. Reflashing this WILL erase any flashed game.
python ./merge_install_bin.py PICODECK.bin recovery/RECOVERY.bin \
    PICODECK_install.bin \
    --base 0x10000000 --recovery-base "$RECOVERY_BASE_ADDR"

python ./bin_to_uf2.py PICODECK_install.bin PICODECK_install.uf2

# "update" UF2 — launcher + recovery, no boot2. For updating without losing
# the currently flashed game or boot2. On legacy installs whose boot2 still
# jumps straight to the launcher, the recovery image is written but dormant;
# the launcher keeps its own UP+X fallback menu for that case.
# Versioned filename so "Upgrade from SD" can show the version; a plain
# PICODECK_update.uf2 copy is kept for BOOTSEL convenience.
python ./bin_to_uf2.py PICODECK_install.bin \
    "PICODECK_update_${VERSION}.uf2" \
    --skip-below "$LAUNCHER_BASE_ADDR"
cp "PICODECK_update_${VERSION}.uf2" PICODECK_update.uf2

rm -f PICODECK_install.bin

echo
echo "=== Build complete ==="
ls -la PICODECK_install.uf2 PICODECK_update*.uf2 recovery/RECOVERY.uf2
