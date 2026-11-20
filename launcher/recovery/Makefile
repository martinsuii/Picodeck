# PicoPad Recovery Makefile
# ------------------------------------------------------------
# Standalone recovery image that boot2 jumps to first. It chain-loads the
# launcher for normal boots, or shows the recovery menu.
# ------------------------------------------------------------

# No boot2 in this image — it lives in the launcher image at 0x10000000.
SRC += src/recovery_main.cpp

# Path to PicoLibSDK root (override via env var or set explicitly)
ifeq ($(PICO_ROOT_PATH),)
$(error PICO_ROOT_PATH not set. Source ../setup_env.sh first or set PICO_ROOT_PATH manually.)
endif

include ${PICO_ROOT_PATH}/Makefile.inc

# Drop boot2 + PicoLibSDK loaders — not part of the recovery image.
ASRC := $(filter-out %boot2_w25q080_RP2040_bin.S,$(ASRC))
ASRC := $(filter-out %loader_picopad10.S,$(ASRC))
ASRC := $(filter-out %loader_picopad08.S,$(ASRC))
ASRC := $(filter-out %loader_picopad20.S,$(ASRC))
ASRC := $(filter-out %loader_picopadhstx.S,$(ASRC))
ASRC := $(filter-out %loader_picopadvga.S,$(ASRC))

# Override the default linker script AFTER the include so the SDK's choice
# doesn't win. Then rewrite the already-expanded LDFLAGS.
ifneq ($(findstring -DRECOVERY_2MB=1,$(EXTRA_CFLAGS)),)
  LDSCRIPT := recovery_m1_2mb.ld
else
  LDSCRIPT := recovery_m1.ld
endif

LDFLAGS := $(patsubst -Wl$(strip ,)-script=%,-Wl$(strip ,)-script=$(LDSCRIPT),$(LDFLAGS))
