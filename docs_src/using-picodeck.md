# Using PicoDeck

PicoDeck is the menu that starts on a PicoPad Arcade. From here you can play
MakeCode Arcade games, use the built-in tools, connect WiFi, install and run
custom apps, and manage the SD card.

## What you need

- A PicoPad Arcade. Native WiFi needs the **Pico W** version.
- A microSD card (FAT32/exFAT; the PicoPad reads 8.3 names).
- A USB cable for flashing and for the desktop companion.

## Install

For a new PicoPad or a device whose boot is broken:

1. Hold **BOOTSEL** while plugging the PicoPad into the computer.
2. A drive called `RPI-RP2` appears.
3. Drag `PicoDeck-1.0.0-install.uf2` onto it.
4. The PicoPad reboots into PicoDeck.

The install image contains boot2, the launcher and the recovery image. It
**replaces any flashed game**.

## Update

1. Copy the update file to the SD card as `update.uf2`.
2. On the PicoPad: **SETTINGS → Update from SD**.
3. Select the file and confirm.

Updates write the **recovery image first**, then the launcher, so a power loss
still leaves a working recovery menu.

> **Version reset:** 1.0.0 is lower than the earlier 1.6.0 build. The updater
> shows `version is not newer (A: force)` — press **A** once.

## First boot

If the SD card contains `FRESH.TXT` or `.fresh`, PicoDeck shows a short tour:

1. Welcome / card is ready
2. **Apps** — custom apps
3. **Arcade** — MakeCode Arcade games
4. **Themes** — `.thm` themes
5. **Backups / Conf** — backups and configuration
6. Done

`A` advances, `X` skips. The marker is deleted afterwards so the tour only
appears once.

If no SD card is inserted, the launcher shows a warning and carries on. WiFi
and the built-in tools still work, but games, themes and apps need the card.

## SD card layout

```
/Apps/                 custom apps
/Arcade/               MakeCode Arcade .uf2 games
/Backups/              game/flash backups
/Conf/                 updates and configuration
/Themes/               .thm themes
/apps.cfg              installed app ids (created by PicoDeck)
/index.txt             optional app update index
/FRESH.TXT, /.fresh    first-boot tour marker
/wifi.cnf              WiFi credentials (two lines: SSID, password)
/wifi_module.uf2       native WiFi module for auto-install
```

Case does not matter to the PicoPad's FAT layer, but keep names 8.3-safe when
possible. The companion's **Prepare SD** creates this structure for you.

## Main menu

```
PLAY
LOAD from SD
TOOLS
FUN
APPS
SETTINGS
```

- **PLAY** launches the flashed game. It is disabled when the native WiFi
  module is installed, because the module and games share the same flash area.
- **LOAD from SD** flashes a `.uf2` game (or the WiFi module).
- **TOOLS / FUN** contain the built-in apps.
- **APPS** is the custom app list and the App Store.
- **SETTINGS** has WiFi, themes, backlight, system info, flash tools and SD
  update.

## Games

PicoDeck patches MakeCode Arcade images for the PicoPad's ST7789 display and
palette. The patches are found by signature, so most MakeCode runtime versions
work.

1. Copy `.uf2` games to `/Arcade` (any folder works with LOAD).
2. **LOAD from SD**, pick the game.
3. Confirm the patch options (the defaults are right for PicoPad).
4. Wait for the flash, then the PicoPad reboots into the game.

A watchdog reset from a running game returns to the game. The hardware RESET
button returns to the launcher.

**Limits**

- Game images must fit below the app slot: **640 KB** in the current layout.
- Loading a game replaces the native WiFi module. Reinstall the module from
  **SETTINGS → WIFI** when you want WiFi back.
- A power loss during the 256-byte boot2 write is the only case that needs
  BOOTSEL recovery; boot2 is written first and verified to make that window as
  small as possible.

## WiFi

Native WiFi runs the Pico W radio on core 1 while the menu runs on core 0.

### Install / reinstall

- Open **SETTINGS → WIFI** with no module installed. PicoDeck offers to flash
  the module.
- Put `wifi_module.uf2` in the card root or `/apps` first.
- The prompt warns that the module replaces a flashed game.
- If the module is installed but failed, the blue WiFi BSOD offers
  **A: reinstall** too.
- You can still use **LOAD from SD** to flash the module manually.

### Connect

- **SETTINGS → WIFI → Scan networks** lists nearby SSIDs.
- **Connect manually** asks for SSID and password.
- **Connect saved** uses `/wifi.cnf` (line 1 = SSID, line 2 = password).
- **Status** shows the real link stage: connecting, getting an IP, wrong
  password, network not found, etc.

Credentials are written to `/wifi.cnf` when you connect manually; the launcher
seeds the module from it at boot.

### Notes

- The launcher keeps the module running when you launch an app.
- **PLAY is disabled** while the module is installed.
- No TLS: apps and the store can use plain HTTP only. The desktop companion
  can proxy HTTPS.
- If WiFi fails, use **SETTINGS → WIFI → Status** and the WiFi BSOD stage /
  crash log to diagnose. A missing or half-flashed module is the usual cause.

## Apps

Custom apps live on the SD card.

### Install from SD

1. Copy an app folder to `/Apps` (it needs `app.ini` + `app.bin`), or copy a
   single `.pap` package there.
2. **APPS → App Store → From SD card**.
3. Pick the app and press **A** to install.
4. **X** returns; the app appears in the APPS list.

### Install from the internet

**APPS → App Store → From internet** downloads from
`http://assets.msuii.dev/picopadapps/` in 3 KB chunks. It needs WiFi connected.
The desktop companion can proxy HTTPS if you run a different store.

### Run and manage

- The APPS list has **App Store** first, then installed apps.
- **A** launches the selected app.
- **Y** opens the app manager:
  - **Launch**
  - **Reinstall** (force the app back into the flash slot)
  - **Edit config** (key/value editor for `config.ini`)
  - **Clear data** (delete config + data, keep the app)
  - **Remove** (remove from the list, keep the files)
- Leaving an app returns to the menu **without rebooting**. The launcher
  snapshots its RAM before the app starts and restores it on exit.
- Apps with `open=txt,bmp,...` in `app.ini` appear in the file manager's
  **Open with** chooser.

### Packages

A `.pap` is a single-file app package. Pack one from the app folder:

```sh
cd apps_sdk
python3 pack.py examples/hello
```

Copy the `.pap` to `/Apps`. The launcher extracts it, verifies `app.ini` and
`app.bin`, and registers it. See [Making apps](making-apps.md).

### Update index

`/apps/index.txt` can mark installed apps as out of date:

```
# id|version|size|url|name
hello|1.0|14014|hello.pap|Hello
```

If the index version is newer than the app's `app.ini` version, APPS shows
`(update)`.

## Companion app

The desktop and Android companion can:

- browse, upload, rename and delete files on the SD card
- **Prepare SD** — wipe the card and create `Apps`, `Arcade`, `Backups`,
  `Conf`, `Themes` and the first-boot marker
- edit themes with a live 320×240 preview
- inspect UF2 files and stage updates
- show device status, launch apps, switch themes, reboot, enter BOOTSEL

The PicoPad exposes **USB CDC at boot** and runs the companion service in the
background. You do **not** need to open the Companion Link screen.

- Linux: AppImage or `node companion/server.js`
- Windows: portable `.exe` (use WiFi TCP for the device connection)
- Android: APK

> **Prepare SD deletes everything on the card.** Back it up first. It only
> works when the card is mounted on the computer, not while it is inside the
> PicoPad.

## Recovery

- Hold **UP + X** at power-on / RESET to open the recovery menu.
- Recovery lives in its own flash partition and is started by boot2 before the
  launcher. A broken launcher cannot take it down.
- Recovery menu: **Boot launcher**, **Boot to BOOTSEL**, **Restart**.
- If recovery itself is broken, use **BOOTSEL** and flash the install image.

## Crash logs

On a launcher, app or WiFi BSOD press **Y**:

- Launcher: IRQ, PC, CFSR, HFSR and registers
- App: app id + the same fault details
- WiFi: stage, reason, module version, fault registers

Logs are written to `/crash/CRASHnnn.TXT` on the SD card. If the card is
read-only or missing, the screen says so. The crash log tries to re-mount the
card first, so a crash before the app initialised the SD can still be saved.

## Troubleshooting

| Problem | Try |
| --- | --- |
| "No SD card" on boot | reinsert the card; run `fsck.vfat -a /dev/sdX` on a PC if it was removed while writing |
| app install fails / SD write error | card is read-only/full or the FAT is damaged; backup, `fsck`, retry |
| WiFi scan times out | check **SETTINGS → WIFI → Status**, reinstall the module, check the BSOD |
| WiFi says wrong password / no network | verify `/wifi.cnf`, 2.4 GHz network, WPA2 |
| app crashes | press **Y** and read `/crash/CRASHnnn.TXT` |
| device won't boot | **UP + X** for recovery, or **BOOTSEL** + install UF2 |
| app shows `(update)` forever | bump `version=` in the app's `app.ini` and the index together |

## Limits

- No app sandbox (no MPU). Only install apps you trust.
- Native WiFi and games share the flash area; PLAY is disabled with the module.
- Game images: 640 KB max in the 1 MB layout.
- Apps: 252 KB max, and the launcher flash is nearly full.
- No TLS in the WiFi module; the direct store uses HTTP.
- Exiting an app writes a 227 KB launcher snapshot to flash, so app launch is a
  little slower and uses flash endurance.
- The Windows companion does not bundle the native serial module in this
  release; use WiFi TCP.
