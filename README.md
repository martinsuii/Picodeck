# PicoDeck

Custom launcher firmware for the [Pajenicko PicoPad](https://picopad.eu) — a handheld
RP2040 game console. The launcher boots first, auto-runs the previously flashed
MakeCode Arcade game on a watchdog reset, or shows a menu where you can flash a
new game from the SD card with on-the-fly ST7789 display patching and palette
remapping.

> Why this exists: stock MakeCode Arcade only supports ILI9341 / ST7735 displays.
> PicoPad uses ST7789. Drag-and-dropping any Arcade game gives you a black screen.
> This launcher patches the runtime binary at flash time so unmodified Arcade UF2s
> just work.

## Features

- **Auto-launch** the flashed game after a watchdog reset
- **Standalone recovery partition** — `boot2` always starts a separate 12 KB
  recovery image first. It chain-loads the launcher on a normal boot, or opens
  the recovery menu if the launcher image is invalid.
- **Recovery menu** — hold **UP + X** at power-on / RESET for
  *Boot launcher / Boot to BOOTSEL / Restart*. Because it lives in its own
  flash partition, a corrupt launcher can't take it down. Kept minimal to fit
  in 12 KB — the debug shell and SD update stay in the launcher's own menus,
  and BOOTSEL reflashes the full install image if the launcher is broken.
- **Hard reset / power-on** → menu (PLAY / LOAD / DEBUG SHELL / USB MSC / BOOTSEL)
- **PLAY** detects what is in flash and dispatches:
  - Arcade game — jumps to `0x10000100` (game vector table)
  - PicoLibSDK app — auto-detected via PPAD magic, jumps to `0x10008000`
  - PicoLibSDK loader without app — jumps to loader, skips BootScreenSaver via
    `WATCHDOG_SCRATCH[4]` magic
- **LOAD from SD** — file picker over the SD card, UF2 selection dialog with
  toggleable patches, flash with progress bar
- **USB MSC** — exposes the SD card as a USB drive (drag-drop file transfer
  between PC and SD)
- **DEBUG SHELL** — interactive shell over USB CDC (`COMx` / `/dev/ttyACMx`)
  with commands: `dump`, `scan`, `readraw`, `readuf2`, `verify`, `flash`,
  `lut`, `cf2`, `bootsel`, `reset`, `exit`
- **REBOOT TO BOOTSEL** — drag-drop a UF2 from the host
- **Button Test** — live state of all eight buttons, with a latched dot for each
  one pressed; hold **LEFT + A** for 1.2 s to exit
- **UF2 Inspector** — validate a `.uf2` from SD and show its version, family,
  address range and block count
- **Themes** — restyle the boot splash, screensaver and menus from `.thm`/`.ini`
  files on the SD card: colours, layout, custom elements, animation timing.
  Pick one under **SETTINGS → Themes**; see [`themes/README.md`](themes/README.md)
- **Companion Link** — drive the launcher from the desktop/Android companion
  app over USB or WiFi (status, apps, themes, reboot, bootloader); see
  [`companion/README.md`](companion/README.md)
- **Native WiFi** — the launcher runs the Pico W's own radio on core 1 while
  the menu stays on core 0, serving Companion Link on port 2323 without an
  ESP32. Flash the module into the game area; see
  [`wifi_module/README.md`](wifi_module/README.md). A standalone-only variant
  lives in [`wifi_link/`](wifi_link/README.md).

Patches applied at flash time (all use **full-region signature scan** so they
survive across MakeCode runtime versions whose offsets shift between builds):

- **ST7789 init** replaces the ILI9341 init sequence (signature
  `EF 03 03 80 02 CF 03`)
- **Palette stub** + 256-byte LUT at `0x10080000` (signature
  `4B 01 0B 43 CC 10 04 43`)
- **CF2 config** at `0x100FF000` — PicoPad-specific configuration block

## Repository layout

```
picopad-arcade-launcher/
├── README.md
├── setup_env.sh / setup_env.bat   ← export PICO_ROOT_PATH
├── launcher/                      ← the launcher firmware
│   ├── c.sh                       ← build script (calls picolibsdk/_c1.sh)
│   ├── config.h                   ← compile-time feature flags
│   ├── Makefile
│   ├── launcher_m1.ld             ← 1 MB layout (CIRCUITPY-safe, default)
│   ├── launcher_m1_2mb.ld         ← 2 MB layout (overwrites CIRCUITPY)
│   ├── recovery/                  ← standalone recovery image (own partition)
│   │   ├── c.sh                   ← recovery build script
│   │   ├── config.h               ← minimal SDK config (no USB/SD/FAT)
│   │   ├── recovery_m1.ld         ← recovery linker script (12 KB slot)
│   │   └── src/recovery_main.cpp  ← boot selector + recovery menu
│   ├── bin_to_uf2.py              ← BIN → UF2 packer (called by c.sh)
│   ├── merge_install_bin.py       ← launcher + recovery → one install image
│   ├── post_process_boot2.py      ← patches boot2 target (recovery) + CRC32
│   └── src/                       ← C++ sources
├── themes/                        ← example theme files + format reference
├── companion/                     ← desktop/Android companion app
│   ├── server.js / web/           ← local HTTP API + UI
│   ├── desktop/                   ← standalone Electron app
│   └── android/                   ← standalone APK (WebView + Java bridge)
├── wifi_module/                   ← launcher-integrated WiFi (core 1 module)
├── wifi_link/                     ← standalone WiFi app (fallback)
├── esp32/wifi_bridge/             ← ESP32 UART/WiFi bridge firmware
└── picolibsdk/                    ← bundled PicoLibSDK (pruned to picopad10)
```

`picolibsdk/` is a vendored, pruned copy of [Panda381/PicoLibSDK](https://github.com/Panda381/PicoLibSDK).
Sample programs and other-device support directories were dropped, leaving the
core SDK + picopad10 device files. One file is patched relative to upstream:
`_sdk/usb_src/sdk_usb_dev.c` exposes the MSC class driver entry that upstream
keeps in `_sdk/usb_src/TODO/`.

## Build prerequisites

- **GCC ARM Embedded 12.x or newer** (`gcc-arm-none-eabi`)
  - Linux: `apt install gcc-arm-none-eabi`
  - macOS: `brew install --cask gcc-arm-embedded`
  - Windows: [ARM GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
  - The `setup_env` scripts honour `ARM_GCC_DIR` if you need to pin a
    specific toolchain (= when multiple are installed). Otherwise whichever
    `arm-none-eabi-gcc` is first on PATH is used.
- **GNU Make** — bundled inside `picolibsdk/_tools/` and added to PATH by
  `setup_env.sh` / `setup_env.bat` automatically
- **Python 3.8+** for the post-processing scripts in `launcher/` (no
  third-party packages required)

GCC 10.x is **not enough** — the linker flag `--no-warn-rwx-segment` and modern
address expressions need binutils 2.39+.

> **Windows users:** ARM GCC cannot handle non-ASCII characters anywhere in the
> path. If your username (= `%USERPROFILE%`) contains any accented characters
> the build will fail with cryptic `cc1.exe: fatal error ... No such file or
> directory` errors. Clone this repo to an ASCII-only path such as
> `C:\projects\picopad-arcade-launcher` (or set up a directory junction with
> `mklink /J`).

## Build

```bash
# Linux / macOS / Git Bash on Windows
cd picopad-arcade-launcher
source setup_env.sh
cd launcher
./c.sh                 # picopad10 (default), 1 MB layout
```

```cmd
:: Windows cmd
cd picopad-arcade-launcher
setup_env.bat
cd launcher
bash c.sh
```

Output:

- `PICODECK_install.uf2` — full boot chain including boot2, launcher and
  the standalone recovery image. Flash this onto a blank PicoPad in BOOTSEL
  mode (drag-drop). **Wipes any flashed game.**
- `PICODECK_update.uf2` — launcher + recovery, no boot2. Keeps the
  currently flashed game. On units whose boot2 still jumps straight to the
  launcher, the recovery image is written but dormant and the launcher's own
  built-in **UP + X** fallback menu is used instead.
- `recovery/RECOVERY.uf2` — recovery image alone, for debugging.

### Flash layout (1 MB default)

```
0x10000000  boot2 (256 B)        → jumps to recovery
0x10000100  game region          (up to 0x100E0000)
0x100E0000  launcher             (108 KB)
0x100FB000  recovery             (12 KB, separate image)
0x100FE000  settings             (4 KB)
0x100FF000  CF2 config           (4 KB)
```

The game region is untouched by the recovery split: recovery is carved out of
the launcher partition. Games occupy `0x10000100..0x100E0000` (~896 KB).
Settings moved from `0x100FC000` to `0x100FE000` (one-time settings reset:
backlight returns to default) because the old sector now sits inside the
recovery partition.

### 2 MB flash layout (no CIRCUITPY)

Default layout occupies the first 1 MB of flash, leaving the second 1 MB
untouched so a CircuitPython `CIRCUITPY` filesystem (at `0x10100000+`)
survives. If you don't need CIRCUITPY and want maximum game space:

```bash
EXTRA_CFLAGS="-DLAUNCHER_2MB=1" ./c.sh
```

This puts the launcher at `0x101E0000` and lets games take ~1.95 MB instead of
~960 KB. The recovery image moves to `0x101FB000` and settings to `0x101FE000`.

### Release build (no debug shell, no USB MSC)

```bash
EXTRA_CFLAGS="-DLAUNCHER_RELEASE" ./c.sh
```

Disables both the USB CDC debug shell and the USB MSC SD reader. Saves ~25 KB
and is what you ship to non-developer users.

## Themes

The launcher looks for theme files in `/themes/` on the SD card. Copy
`themes/showcase.thm` (or `themes/neon.thm`) there, open
**SETTINGS → Themes**, and press **A** on a name. The selection is stored in
`/themes/active.cfg` and reloaded at boot.

A theme can change colours, the boot splash, the screensaver and the menu
layout. It supports colour variables, named colours, text alignment, marquee /
blinking text, shapes (rect, frame, line, circle, disc), live clock / weather /
status elements, and timing keys for the splash and screensaver. Themes replace
the built-in boot/saver element lists by default; set `boot_append=1` or
`saver_append=1` to extend them instead.

The complete key and element reference is in
[`themes/README.md`](themes/README.md).

## Usage

1. Flash `PICODECK_install.uf2` onto your PicoPad in BOOTSEL mode.
2. Put MakeCode Arcade `.uf2` games onto an SD card (any folder layout).
3. Insert the SD card, power-cycle the PicoPad — the launcher menu appears.
4. **LOAD from SD** → pick a game → confirm patches → wait for flash to finish.
5. The PicoPad reboots into the game. From now on, watchdog resets bring you
   straight back to the game; pressing the hardware RESET button gets you to
   the launcher menu.
6. If the launcher ever fails to boot, the recovery image catches it and shows
   the recovery menu. Hold **UP + X** at power-on to open that menu on purpose.

## Credits

- [Panda381/PicoLibSDK](https://github.com/Panda381/PicoLibSDK) — the alternative
  RP2040/RP2350 SDK that this project builds on
- [Pajenicko PicoPad](https://github.com/pajenicko/picopad) — the hardware
- [microsoft/uf2](https://github.com/microsoft/uf2) — UF2 format spec and tools
- [microsoft/pxt-arcade](https://github.com/microsoft/pxt-arcade) — MakeCode
  Arcade runtime
