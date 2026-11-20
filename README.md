# PicoDeck

PicoDeck is a custom launcher and app platform for the
[Pajenicko PicoPad](https://picopad.eu), an RP2040 handheld. It runs on the
PicoPad Arcade (Pico W for native WiFi) and turns the device into a game
launcher, tool box and small app computer.

> **Derived from the PicoPad Arcade Launcher by MakerClassCZ.**
> The base launcher, the UF2 flashing/update tooling, the MakeCode Arcade
> display patching and the palette/CF2 work all come from
> <https://github.com/MakerClassCZ/picopad-arcade-launcher>.
> PicoDeck adds native WiFi, custom apps, the app store, the always-on
> companion and the recovery/snapshot work on top of that base.

## What it does

- Boots first and shows a launcher menu: **PLAY / LOAD / TOOLS / FUN / APPS /
  SETTINGS**.
- Flashes MakeCode Arcade `.uf2` games from the SD card and patches them at
  flash time so they run on the PicoPad's ST7789 display.
- Runs the Pico W's own WiFi radio on core 1 while the menu stays on core 0.
- Runs custom SD-card apps with a small app API.
- Serves an always-on USB companion protocol, so the desktop/Android app works
  without opening a special screen on the PicoPad.
- Keeps a separate recovery image so a broken launcher can still be recovered.

## Documentation

- **[Using PicoDeck](https://msuii.dev/Picodeck/using.html)** — install, update, games, WiFi,
  apps, store, companion, recovery and troubleshooting.
- **[Making apps](https://msuii.dev/Picodeck/apps.html)** — app SDK, API, `.pap` packages,
  examples and limits.
- **[Contributing](https://msuii.dev/Picodeck/contributing.html)** — builds, flash layout, safety rules,
  testing and releases.

## Features

### Games

- MakeCode Arcade UF2 support with signature-based patches that survive
  different MakeCode runtime versions:
  - ST7789 display init replaces the ILI9341 sequence
  - palette stub + 256-byte LUT at `0x10080000`
  - CF2 device config at `0x100FF000`
- **LOAD from SD** picks a `.uf2`, lets you toggle patches, flashes with a
  progress bar and verifies boot2.
- **PLAY** launches the flashed game; a watchdog reset from a game returns to
  it.
- Games and the native WiFi module share the game area, so **PLAY is disabled
  while the WiFi module is installed**.
- Game images are limited to **640 KB** in the current layout because the app
  slot lives above them.

### Recovery and safety

- `boot2` jumps to a separate **12 KB recovery image**, which then chain-loads
  the launcher. A bad launcher never takes recovery down with it.
- Hold **UP + X** at power-on/RESET for the recovery menu:
  *Boot launcher / Boot to BOOTSEL / Restart*.
- Launcher updates write the **recovery image first**, then the launcher. A
  power loss leaves recovery intact.
- Game flashes write **boot2 first**, verify it, then write the game.
- **Flasher trasher** never erases sector 0; it invalidates the game vector and
  keeps boot2.
- Full-screen BSODs for the launcher, apps and WiFi module. **Y saves a crash
  log** to `/crash/CRASHnnn.TXT` on the SD card.

### Native WiFi

- The Pico W radio runs on **core 1** from a separate module image
  (`wifi_module`, currently 1.2.20). The launcher stays on core 0.
- WiFi menu: Status, Scan, Connect manually, Connect saved, Disconnect.
- Credentials come from `/wifi.cnf` (two lines: SSID, password).
- **Auto-install:** open **SETTINGS → WIFI** with no module installed and it
  offers to flash `/wifi_module.uf2` (or `/apps/wifi_module.uf2`) for you. The
  WiFi BSOD also offers a reinstall.
- Raw TCP for apps and a plain HTTP GET client in the module. No TLS.
- The launcher keeps the module alive while apps run; leaving an app does not
  restart the device.

### Custom apps

- Apps are folders in `/apps` on the SD card:

  ```
  /apps/
    apps.cfg                 installed app ids
    index.txt                optional update index
    hello/
      app.ini                name=Hello
                             bin=app.bin
                             version=1.0
                             open=txt            (optional file types)
      app.bin                compiled app
      config.ini             per-app settings
      data/                  per-app files
  ```

- **APPS → App Store → From SD card** installs folders or single-file `.pap`
  packages.
- **APPS → App Store → From internet** downloads from
  `http://assets.msuii.dev/picopadapps/` as 3 KB chunks (plain HTTP; the
  desktop companion can proxy HTTPS if needed).
- **APPS** lists installed apps and launches them; **Y** opens the app manager:
  Launch, Reinstall, Edit config, Clear data, Remove.
- Apps can use the PicoLibSDK display/input/SD APIs plus:
  - system: exit, reboot, recovery, BOOTSEL, custom BSOD, crash log
  - config: `AppConfigRead/Write`, `AppDataPath`
  - WiFi: status, scan, connect, disconnect, `AppHttpGet`
  - raw TCP: `AppNetOpen/Send/Recv/Close` (module 1.2.18+)
  - theme: `AppTheme(...)`, `AppThemeName()`
  - files: `AppArgPath()` when opened from the file manager
- **Leaving an app returns to the menu without rebooting.** The launcher
  snapshots its RAM to a reserved flash area before launching and the app's
  `AppExit()` restores it. The WiFi module keeps running.
- Apps are built with `apps_sdk/`; see [`apps_sdk/README.md`](apps_sdk/README.md).

### Companion

- Desktop (Linux/Windows) and Android companion:
  - browse/upload/rename/delete files on the SD card
  - **Prepare SD** (wipe + create `Apps`, `Arcade`, `Backups`, `Conf`,
    `Themes` and the first-boot marker)
  - theme editor with a 320×240 preview
  - UF2 inspection, staging updates
  - device status, app launching, **Companion Link**
- The PicoPad boots as USB CDC and runs the companion command service in the
  background, so no Companion Link screen is needed on the PicoPad.
- The Windows companion is a portable `.exe`; Linux is an AppImage; Android is
  an APK. See [`companion/README.md`](companion/README.md).

### First boot and SD card

- If no SD card is present, the launcher shows a clear warning and keeps
  WiFi/tools usable.
- The companion's **Prepare SD** creates:

  ```
  /Apps       custom apps
  /Arcade     MakeCode Arcade .uf2 games
  /Backups    game/flash backups
  /Conf       updates and configuration
  /Themes     .thm themes
  /FRESH.TXT  first-boot marker (8.3-safe)
  /.fresh     same marker for PCs
  ```

- If `FRESH.TXT` or `.fresh` exists at boot, a short 6-page tutorial explains
  the folders; it deletes the marker when finished.

### Themes and tools

- Themes from `.thm` files on the SD card: colours, boot splash, screensaver,
  menu layout, text and shapes. See [`themes/README.md`](themes/README.md).
- TOOLS: GPIO control, Rubber Ducky, SD file manager, image viewer, WiFi,
  Button Test, UF2 Inspector, Companion Link, and the SD card reader/debug
  shell in development builds.
- FUN: Snake, USB gamepad, mouse jiggler, media remote, keyboard pranks, Paint,
  Calculator, Stopwatch, Timer, MIDI pad.

### Version note

PicoDeck **1.0.0** is a version reset after the earlier 1.x builds. The updater
compares versions, so applying 1.0.0 over 1.6.0 shows
`version is not newer (A: force)` — press **A** once.

## Flash layout (1 MB default)

```
0x10000000  boot2 (256 B)              -> recovery
0x10000100  game / WiFi module area     up to 0x100A0000 (640 KB)
0x10050000  launcher RAM snapshot      0x37800, used only while an app runs
0x100A0000  custom app slot             252 KB
0x100DF000  app install record          4 KB
0x100E0000  launcher                    108 KB
0x100FB000  recovery                    12 KB
0x100FE000  settings                    4 KB
0x100FF000  CF2 config                  4 KB
```

The second 1 MB of flash is left untouched (CIRCUITPY-safe) in the default
layout. A 2 MB layout is available with `-DLAUNCHER_2MB=1`.

## Build

### Launcher and recovery

```bash
source setup_env.sh
cd launcher
EXTRA_CFLAGS="-DLAUNCHER_RELEASE" ./c.sh
```

Outputs in `launcher/`:

- `PICODECK_install.uf2` — boot2 + launcher + recovery (BOOTSEL install)
- `PICODECK_update_1.0.0.uf2` — launcher + recovery (SD update)
- `PICODECK_update.uf2` — convenience copy
- `recovery/RECOVERY.uf2` — recovery image alone

### WiFi module

```bash
cd wifi_module
./build.sh
```

Output: `wifi_module/dist/PICODECK_wifimod_1.2.20.uf2`. Copy it to the SD as
`wifi_module.uf2` for the in-launcher installer.

### Apps

```bash
cd apps_sdk
./build.sh examples/hello       # builds examples/hello/app.bin
python3 pack.py examples/hello  # builds examples/hello/hello.pap
```

### Companion

```bash
cd companion
node server.js                  # web UI + API on http://localhost:8787
node desktop/build.js           # Linux Electron folder + tarball
node android/build.js           # Android APK (needs an Android SDK)
```

Release builds use electron-builder for the Windows portable `.exe` and the
Linux AppImage; the config lives in `companion/package.json`.

### Prerequisites

- ARM GNU Toolchain 12.x or newer (`arm-none-eabi-gcc`)
- GNU Make and Python 3.8+
- Node.js 18+ for the companion
- Android SDK (build-tools 34, platform android-34) for the APK
- Electron (or electron-builder) for the desktop builds

## Usage

1. **New device:** flash `PICODECK_install.uf2` in BOOTSEL mode (drag it onto
   the `RPI-RP2` drive).
2. **Existing PicoDeck:** copy the update to the SD as `update.uf2`, then
   **SETTINGS → Update from SD**.
3. Copy the WiFi asset to the SD as `wifi_module.uf2`; open
   **SETTINGS → WIFI** and accept the install prompt.
4. Put MakeCode Arcade games in `/Arcade` and load them with **LOAD from SD**.
5. Put apps in `/Apps`; install them from **APPS → App Store → From SD card**.
6. If the launcher breaks, hold **UP + X** at power-on for recovery, or flash
   the install image via BOOTSEL.

## Repository layout

```
├── launcher/            launcher firmware, recovery, UF2 tooling
├── wifi_module/         native WiFi module (core 1)
├── wifi_link/           standalone WiFi test app (not built by default)
├── esp32/               legacy ESP32 bridge firmware (no longer built)
├── apps_sdk/            app API, linker script, packer and examples
├── companion/           desktop/Android companion, server, web UI
├── themes/              example themes and theme format docs
├── docs/                static HTML docs site
├── docs_src/            markdown docs source + build.py
├── .github/             issue forms and PR template
├── picolibsdk/          vendored, pruned PicoLibSDK (picopad10)
└── setup_env.sh/.bat    PICO_ROOT_PATH setup
```

## Credits

PicoDeck would not exist without these projects:

- **[MakerClassCZ/picopad-arcade-launcher](https://github.com/MakerClassCZ/picopad-arcade-launcher)**
  — the base PicoPad Arcade launcher, UF2 flashing/update tooling and the
  MakeCode Arcade display/palette patching. PicoDeck is derived from it.
- [Panda381/PicoLibSDK](https://github.com/Panda381/PicoLibSDK) — the alternative
  RP2040/RP2350 SDK used by the launcher and apps.
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) and the
  CYW43 driver — used by the native WiFi module.
- [Pajenicko PicoPad](https://github.com/pajenicko/picopad) — the hardware.
- [microsoft/uf2](https://github.com/microsoft/uf2) — UF2 format and tools.
- [microsoft/pxt-arcade](https://github.com/microsoft/pxt-arcade) — MakeCode
  Arcade runtime and games.

## Limits and honest notes

- **No app sandbox.** The RP2040 has no MPU. A malicious app can write flash and
  brick boot2. Only install apps you trust; BOOTSEL is the last recovery.
- **No TLS in the WiFi module.** The direct app store is plain HTTP; the
  desktop companion can fetch HTTPS and proxy packages.
- **App snapshot flash writes.** Returning from an app without a reboot saves
  ~227 KB to flash on launch, which costs a second or two and some flash wear.
- **Windows companion** does not bundle the native `serialport` module in this
  release; use the PicoPad's WiFi TCP link on Windows.
- The legacy ESP32 WiFi/Bluetooth tools are no longer part of the launcher.
