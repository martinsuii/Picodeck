#!/usr/bin/env bash
# Build script for the PicoPad recovery image.
#   Usage: ./c.sh [picopad10|...]
# Produces RECOVERY.bin (and RECOVERY.uf2) at the recovery partition base.

set -e

DEVICE="${1:-picopad10}"

if [ -z "$PICO_ROOT_PATH" ]; then
    echo "ERROR: PICO_ROOT_PATH not set. Run: source ../../setup_env.sh"
    exit 1
fi

export TARGET="RECOVERY"
export GRPDIR="PICODECK"
export MEMMAP=""
export EXTRA_CFLAGS="${EXTRA_CFLAGS:-} -flto"

if [[ "${EXTRA_CFLAGS}" == *-DLAUNCHER_2MB=1* || "${EXTRA_CFLAGS}" == *-DRECOVERY_2MB=1* ]]; then
    export EXTRA_CFLAGS="${EXTRA_CFLAGS} -DRECOVERY_2MB=1"
    RECOVERY_BASE_ADDR="0x101FB000"
    LAYOUT_LABEL="2 MB"
else
    RECOVERY_BASE_ADDR="0x100FB000"
    LAYOUT_LABEL="1 MB"
fi
echo "Layout: $LAYOUT_LABEL — recovery at $RECOVERY_BASE_ADDR"

build_log=$("$PICO_ROOT_PATH/_c1.sh" "$DEVICE" 2>&1) || true
echo "$build_log"

if echo "$build_log" | grep -qE 'error:|fatal error|undefined reference|ld returned [1-9]|region .* overflowed'; then
    echo "ERROR: recovery compilation/link failed — see build log above"
    rm -f RECOVERY.bin
    exit 1
fi

if [ ! -f "RECOVERY.bin" ]; then
    echo "ERROR: RECOVERY.bin not produced"
    exit 1
fi

# Pack the recovery-only bin into a UF2 with absolute target addresses.
python ../bin_to_uf2.py RECOVERY.bin RECOVERY.uf2 --base "$RECOVERY_BASE_ADDR"

echo
echo "=== Recovery build complete ==="
ls -la RECOVERY.bin RECOVERY.uf2
