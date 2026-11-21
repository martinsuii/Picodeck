# Contributing to PicoDeck

Thanks for helping. PicoDeck is a PicoPad launcher plus a module, an app SDK
and a companion. This page covers the builds, the flash layout rules, the app
API compatibility rules and the test checklist.

PicoDeck is derived from the PicoPad Arcade Launcher by MakerClassCZ
(<https://github.com/MakerClassCZ/picopad-arcade-launcher>). The base launcher,
UF2 tooling and MakeCode Arcade patching come from there.

## Repository map

```
launcher/        launcher firmware, recovery image, UF2 tooling
wifi_module/     native WiFi module (core 1, Pico SDK)
apps_sdk/        app API, linker script, packer, examples
companion/       Node server + web UI, desktop Electron, Android
themes/          example themes and theme format
esp32/           legacy ESP32 firmware (not built by the launcher)
wifi_link/       standalone WiFi test app (not part of the launcher build)
picolibsdk/      vendored, pruned PicoLibSDK for picopad10
docs/            user and developer docs
.github/         issue templates and PR template
```

## Prerequisites

- ARM GNU Toolchain **12.x or newer** (`arm-none-eabi-gcc`)
- GNU Make, Python 3.8+
- Node.js 18+ for the companion
- Android SDK (build-tools 34, platform android-34) for the APK
- Electron and/or electron-builder for the desktop builds

You need a **Pico W** PicoPad for native WiFi. The launcher still boots on a
non-W PicoPad with WiFi disabled.

## Build

### Launcher and recovery

```bash
source setup_env.sh
cd launcher
EXTRA_CFLAGS="-DLAUNCHER_RELEASE" ./c.sh
```

Outputs:

- `PICODECK_install.uf2` — boot2 + launcher + recovery
- `PICODECK_update_1.0.0.uf2` — launcher + recovery (and `PICODECK_update.uf2`)
- `recovery/RECOVERY.uf2`

Development builds (omit `-DLAUNCHER_RELEASE`) include the USB CDC debug shell
and USB MSC. Release builds omit them to save flash.

### WiFi module

```bash
cd wifi_module
./build.sh
```

Uses the Pico SDK (`PICO_SDK_PATH`, default `/usr/share/pico-sdk`) and board
`pico_w`. Output: `dist/PICODECK_wifimod_1.2.20.uf2`.

### Apps

```bash
cd apps_sdk
./build.sh examples/hello
python3 pack.py examples/hello
```

### Companion

```bash
cd companion
node server.js                 # http://localhost:8787
node desktop/build.js          # Linux Electron folder + tarball
node android/build.js          # Android APK
```

Release desktop builds use electron-builder (config in
`companion/package.json`):

```bash
cd companion
npx electron-builder --linux AppImage --win portable --x64
```

The Windows build skips native module rebuilds; the `serialport` optional
dependency is not bundled there.

## Flash layout (1 MB default)

```
0x10000000  boot2
0x10000100  game / WiFi module area      up to 0x100A0000 (640 KB)
0x10050000  launcher RAM snapshot        0x37800 (while an app runs)
0x100A0000  custom app slot              252 KB
0x100DF000  app install record           4 KB
0x100E0000  launcher                     108 KB
0x100FB000  recovery                     12 KB
0x100FE000  settings                     4 KB
0x100FF000  CF2                          4 KB
```

The 2 MB layout (`-DLAUNCHER_2MB=1`) moves the launcher to `0x101E0000`; the
app SDK currently targets the 1 MB layout.

### Invariants — do not break these

- **boot2 is the only path to recovery.** Never erase sector 0 in the trasher.
- **Game flashing writes boot2 first, verifies it, then writes the game.**
- **Launcher updates write recovery first, then the launcher.**
- **App installs only touch the app slot and record.** Never boot2, recovery,
  launcher, module or CF2.
- **Games/modules must not erase above `GAME_ERASE_HI` (the app slot).**
- **The launcher snapshot must be outside the app slot and the module area.**
- The launcher and recovery addresses are shared across `launcher/config.h`,
  `launcher/*.ld`, `launcher/recovery/config.h`, `launcher/c.sh`,
  `launcher/merge_install_bin.py`, `app_shared.h` and `apps_sdk/app.ld`.
  Changing one without the others bricks the chain.

## App API compatibility

`app_shared.h` is the contract between the launcher and apps.

- `app_info_t` is **append-only**. New fields go at the end, and old apps must
  still work.
- `APP_API_VERSION` is checked by `Info()`. Bump it only with a migration plan.
- Extended fields are gated by `APP_INFO_EXT_MAGIC`; old launchers don't write
  them and new apps fall back to defaults.
- `APP_INFO_ADDR`, `APP_SLOT_BASE`, `APP_SLOT_SIZE`, `APP_RECORD_ADDR`,
  `APP_NET_BUF_ADDR` and `LAUNCHER_SNAPSHOT_*` are fixed addresses. Changing
  them requires rebuilding every app and updating `apps_sdk/app.ld`.
- Keep `app_info_t` out of app RAM: the app heap ends at `0x20037800`.

## Coding style

- Match the surrounding file: 4 spaces, C++ for the launcher, C for the app SDK
  and the module.
- Avoid `printf`/`malloc` in the launcher where a static buffer works. RAM is
  tight; check `arm-none-eabi-size`.
- Use the theme colours (`g_theme.*`, `AppTheme(...)`) rather than hard-coded
  colours in UI.
- Keys: no assumptions about B; it may be broken. Use A/X/UP/DOWN/Y.
- Any flash operation must pause core 1 first and resume after (unless the
  module is being replaced).
- Never call `RuntimeInit`/`DeviceInit`/`BatInit`/`DispInit` from an app.
- Keep new launcher code within the flash budget; release builds are close to
  the 108 KB launcher image.

## Test checklist

Before a release, on real hardware:

- [ ] Install UF2 on a blank device via BOOTSEL.
- [ ] Update from SD; recovery still boots.
- [ ] Boot with no SD → warning; insert card → normal.
- [ ] First boot with `FRESH.TXT` → tutorial; marker deleted.
- [ ] Game flash from SD; PLAY works; watchdog reset relaunches.
- [ ] WiFi module install prompt; scan; connect; status stages; link up.
- [ ] Flash a game over the module; PLAY works; reinstall module; WiFi returns.
- [ ] App install from SD folder and `.pap`; launch; exit without reboot.
- [ ] App manager: reinstall, edit config, clear data, remove.
- [ ] App store online (HTTP) install/update.
- [ ] Crash log: force a BSOD, Y writes `/crash/CRASHnnn.TXT`.
- [ ] Companion connects without the Companion Link screen; Prepare SD on a
  spare card.
- [ ] Recovery: UP+X at power-on; BOOTSEL.
- [ ] Power loss during update and during a game flash (expected: recovery).

## App store

The catalogue is served at <https://pico.msuii.dev>. The package tree lives at
`http://pico.msuii.dev/assets/apps/` and `.../assets/themes/`.

- The site and moderation server run separately from this repository; they
  write `index.txt` and the chunk files, and nginx serves that tree directly.
- `index.txt` format: `id|version|size|url|name`.
- The PicoPad fetches over **plain HTTP**; package files are split into 3 KB
  chunks (`hello.pap.000`, `.001`, ...) because the module's HTTP buffer is
  4 KB. Theme packages use the same chunking.
- `assets.msuii.dev/picopadapps/` is kept as an alias for firmware 1.0.0,
  which hardcodes that host. It must stay an alias, not a redirect: the
  firmware only parses `200 OK`.
- Bump the app's `app.ini` `version=` and the index version together.
- Apps are native ARM code, so review the package contents before approving.
  Themes cannot execute anything.

## Git and releases

- Don't commit build outputs. The repo `.gitignore` covers `build/`, UF2/BIN/
  ELF/MAP, `*.pap`, `node_modules`, `companion/dist`.
- Commit messages: short imperative summary (`wifi: fix scan completion`,
  `apps: add clear data`).
- Releases: build launcher/module/apps, run `android/build.js`, build the
  AppImage and Windows EXE with electron-builder, generate `SHA256SUMS`, then
  `gh release create vX.Y.Z --notes-file ...` with the assets.
- Issue and PR templates live in `.github/`.

## Reporting issues

Use the GitHub issue templates:

- **Bug report** — launcher/game/WiFi/companion problems.
- **App or store issue** — a custom app or the app catalogue.
- **Feature request** — ideas.

Include the PicoDeck version, module version, hardware (Pico W?), SD size and
the crash log when there is one. Please don't paste secrets from `/wifi.cnf`.

## Credits

- [MakerClassCZ/picopad-arcade-launcher](https://github.com/MakerClassCZ/picopad-arcade-launcher) — base launcher, UF2 tooling, MakeCode Arcade patching
- [Panda381/PicoLibSDK](https://github.com/Panda381/PicoLibSDK)
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [Pajenicko PicoPad](https://github.com/pajenicko/picopad)
- [microsoft/uf2](https://github.com/microsoft/uf2)
- [microsoft/pxt-arcade](https://github.com/microsoft/pxt-arcade)
