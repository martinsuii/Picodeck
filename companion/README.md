# PicoDeck Companion

Manage the PicoDeck from a computer or a phone:

- browse, upload, rename and delete files on the SD card
- inspect and edit `.thm` themes with a live 320×240 preview
- check a UF2 before flashing and stage it as `update.uf2`
- connect to the device over USB or WiFi, read its status, list and launch
  apps, switch themes, reboot, and enter the bootloader

There are three ways to run it:

| Build | What it is | Where it runs |
| --- | --- | --- |
| `server.js` + `web/` | Local HTTP server, opens in any browser | Computer, also reachable from a phone on the LAN |
| `desktop/` | Standalone Electron app around the same server and UI | Linux desktop |
| `android/` | Standalone WebView app with a native Java bridge | Android phone/tablet |

The PicoPad exposes USB CDC at boot and runs the companion service in the
background, so the desktop/Android app works without opening the **Companion
Link** screen. That screen is still available in TOOLS for diagnostics.

---

## Quick start (browser)

```sh
cd companion
npm start          # http://localhost:8787
```

The server binds `0.0.0.0` so a phone on the same network can open the
Network URL printed at startup. There is no authentication; only run it on a
network you trust.

If USB serial control is wanted, install the optional native module:

```sh
npm install
```

Without it the Linux server falls back to `stty` on `/dev/ttyACM*` and
`/dev/ttyUSB*`. WiFi control works either way.

Useful flags:

```sh
node server.js --port 9000 --host 127.0.0.1
PORT=9000 node server.js
```

---

## Standalone Linux app

```sh
cd companion
node desktop/build.js
```

This creates:

- `dist/linux/PicoDeckCompanion/` – app folder with a bundled Electron runtime
- `dist/picodeck-companion-linux-x64.tar.gz` – same thing, compressed
- `dist/linux/PicoDeckCompanion/install.sh` – copies to `~/.local/opt` and adds
  a desktop entry

Run it directly:

```sh
./dist/linux/PicoDeckCompanion/picodeck-companion --no-sandbox
```

`--no-sandbox` is used because the packaged runtime does not ship a setuid
`chrome-sandbox` helper. The app starts the same local server on a random
port, opens its own window, and prints the LAN URL so a phone can connect to
the same UI.

The Electron binary is taken from `/usr/lib/electron` by default. Point
`ELECTRON_DIR` at another full Electron runtime if needed:

```sh
ELECTRON_DIR=/path/to/electron node desktop/build.js
```

---

## Standalone Android app

```sh
cd companion
node android/build.js
adb install -r dist/PicoDeckCompanion-android-1.2.9.apk
```

The build script uses the Android SDK directly (no Gradle). It expects
`ANDROID_HOME` or `ANDROID_SDK_ROOT`; the default is `/home/martin/Android`.
It needs build-tools 34, platform 34 and a JDK. `cmdline-tools/latest/lib/r8.jar`
is used for dexing because the `d8` in build-tools 34 crashes on inner classes
with current JDKs.

The APK is debug-signed with `android/debug.keystore`; that file and the
`build/` directory are generated locally and ignored by git.

### Using the Android app

1. Start the app.
2. On the SD card tab, tap **Choose SD folder…**, then pick the card root
   (for example the `PICOPAD` volume). Android's Storage Access Framework
   grants access; no storage permission is needed.
3. Pick **USB** or **WiFi** on the Device tab and connect.

USB needs an OTG adapter and a data-capable cable. The app asks Android for
permission to use the PicoPad's CDC port the first time.

WiFi needs the ESP32 bridge running the updated firmware with the TCP link
server on port 2323. The phone and the ESP32 must be on the same network.
Start the link from the PicoPad side with `LINK START` (the Companion Link app
does this automatically), then connect to the ESP32's IP from the app.

There is also a native option that skips the ESP32 entirely: flash
[`wifi_link`](../wifi_link/README.md) into the game area. It runs the same
protocol on the Pico W's own CYW43439 radio and listens on port 2323, so the
app connects to the PicoPad's IP directly.

The app stores its settings and the chosen SD folder in `localStorage`. All
file work still goes through SAF, so backups and uploads land where you pick.

---

## Firmware: Companion Link and WiFi

The launcher exposes the protocol through:

- **USB CDC** – connect the PicoPad directly; commands go straight to the link.
- **UART + ESP32** – the ESP32 bridge forwards TCP lines to the PicoPad with an
  `@` prefix and forwards `@`-prefixed UART lines back to TCP clients.

Flash a release launcher image (`PICODECK_install.uf2` or a newer
`PICODECK_update_*.uf2`) and open **TOOLS → Companion Link** on the
device. Send `LINK START` (or let the app do it) to bring up the ESP32 TCP
server.

Build the ESP32 bridge with the huge-app partition layout:

```sh
arduino-cli compile \
  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
  esp32/wifi_bridge
```

The default 1.2 MB partition is too small for the WiFi + BLE + web server
firmware.

---

## SD card layout

```
/themes/            .thm files and active.cfg
/games/             game folders/files
/update.uf2         staged launcher update (deleted after flashing)
```

The launcher also accepts `/Themes` on older cards. Theme uploads are capped at
4095 bytes by the theme engine; the Companion Link `THEME PUT` upload accepts
up to 8192 bytes. Keep theme text under 4 KB.

---

## Theme studio

The Themes tab can read themes from the card or from the bundled examples.
Editing writes straight to the card; **Set active** writes
`themes/active.cfg`. The preview uses the launcher's 8×8 font and the same
parser as the firmware.

**Send to device** streams the current editor text over the active link using
`THEME PUT`, so a theme can be changed without removing the SD card. The
device writes to `/themes/<name>.thm` and reloads the active theme.

See `../themes/README.md` for the theme file format.

---

## Updates

The Updates tab:

- validates a UF2 file block by block (magic, family, payload size, block
  count, address layout)
- shows the version tag, block count, address range and any gaps
- **Stage as update.uf2** copies it to the SD card root and keeps
  `update.uf2.bak` if a staged update already exists

The launcher scans the card root on boot and flashes `update.uf2`
automatically, then deletes it.

---

## Protocol

See [protocol.md](protocol.md) for the USB/WiFi framing, every command, the
reply formats and examples.

---

## Troubleshooting

**No SD card detected.**  
Use **Choose SD folder…** (Android) or set the path in the SD tab (desktop).
On Linux the card is usually under `/run/media/<user>/<label>`.

**USB serial is not available on Linux.**  
Run `npm install` in `companion/`. The fallback needs `stty` and access to the
`/dev/ttyACM*` device (add your user to the `uucp`/`dialout` group).

**The phone cannot reach the ESP32.**  
Check that the ESP32 and phone share a network, that `LINK START` was sent,
and that TCP 2323 is not blocked by an AP isolation setting. Also check the
ESP32's IP with `WIFI STATUS` or from the PicoPad's ESP32 page.

**The Android app cannot see the card.**  
Pick the card root, not a subfolder. If the picker shows only recent
documents, use the three-dot menu and enable internal storage/SD card access.

**The APK build fails at d8.**  
Make sure `cmdline-tools/latest/lib/r8.jar` exists. If it does not, install
`cmdline-tools;latest` with `sdkmanager`.

---

## Layout

```
companion/
  server.js              HTTP + SSE API and static server
  lib/config.js          saved paths, SD detection
  lib/sdcard.js          file operations, backup/restore, staging
  lib/theme.js           theme parser shared by server and web UI
  lib/uf2.js             UF2 inspector shared by server and web UI
  lib/device.js          USB serial / Linux tty / WiFi TCP
  web/                   the UI (same files on desktop and Android)
  web/local-api.js       fetch/EventSource shim used by the Android app
  desktop/main.js        Electron entry point
  desktop/build.js       Linux packaging
  android/java/          MainActivity, NativeBridge, SAF, USB CDC, TCP
  android/build.js       APK build with aapt2/javac/r8/zipalign/apksigner
```
