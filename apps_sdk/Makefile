# PicoPad custom app build
# ------------------------------------------------------------
# Usage (normally via build.sh):
#   make TARGET=app APP_CSRC="/path/to/MyApp/main.c" \
#        PICO_ROOT_PATH=../picolibsdk DEVICE=picopad10 DEVCLASS=picopad
#
# Output: ./app.bin (raw image linked at APP_SLOT_BASE) -> copied to the
# app folder as app.bin by build.sh.
# ------------------------------------------------------------

PICO_ROOT_PATH ?= ../picolibsdk
DEVICE         ?= picopad10
DEVCLASS       ?= picopad
TARGET         ?= app

# SDK runtime pieces we replace (radio-safe init, compact fatal screen).
CSRC += app_runtime.c
CSRC += app_api.c
CSRC += $(APP_CSRC)
SRC  += $(APP_SRC)

# PicoLibSDK build system
include ${PICO_ROOT_PATH}/Makefile.inc

# Drop the boot3 loader (we are loaded by the launcher, not booted directly).
ASRC := $(filter-out %loader_picopad10.S,$(ASRC))
ASRC := $(filter-out %loader_picopad08.S,$(ASRC))
ASRC := $(filter-out %loader_picopad20.S,$(ASRC))
ASRC := $(filter-out %loader_picopadhstx.S,$(ASRC))
ASRC := $(filter-out %loader_picopadvga.S,$(ASRC))

# Our own RuntimeInit / RuntimeTerm / exit and FatalErrorMsg.
CSRC := $(filter-out %/sdk_runtime.c,$(CSRC))
CSRC := $(filter-out %/sdk_fatal.c,$(CSRC))

# App linker script (must be overridden after Makefile.inc, same reason as
# the launcher: LDFLAGS expanded its default -script= at include time).
LDSCRIPT := app.ld
LDFLAGS := $(patsubst -Wl$(strip ,)-script=%,-Wl$(strip ,)-script=$(LDSCRIPT),$(LDFLAGS))
