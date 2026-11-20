# WiFi module (native, launcher-integrated)

This is the native WiFi build that runs **inside the launcher**. It is not a
separate mode: the launcher keeps running on core 0 while this module runs on
core 1, always on.

```
core 0: launcher        menu, SD, USB, display, Companion Link
core 1: WiFi module     CYW43439 driver, lwIP, TCP server on 2323
```

The launcher detects the module in the game flash region at boot, starts it on
core 1, and reads its status from a fixed shared RAM page. There is no PLAY
step and no separate app.

## Files

- `wifi_module.c` — radio + lwIP + Companion Link protocol; entry point
  `wifi_module_entry()`.
- `wifi_shared.h` — shared status block and command/reply rings (included by
  both the module and the launcher).
- `lwipopts.h` — trimmed lwIP configuration.
- `CMakeLists.txt`, `build.sh` — Pico SDK build. The RAM window is relocated
  with linker `--defsym` options so the module's BSS cannot collide with the
  launcher.

The launcher side lives in `launcher/src/native_wifi.cpp`.

## Memory layout

| Region | Address | Use |
| --- | --- | --- |
| Launcher RAM | `0x20000000..0x20039000` | launcher BSS + heap |
| Shared page | `0x20039000..0x20039FFF` | status + rings |
| Module RAM | `0x2003A000..0x20040000` | module `.data` + `.bss` (24 KiB) |

The launcher's RAM region was reduced from 256 KiB to 228 KiB to make room.
The hole is filled by trimming three buffers: the ESP32 web server page
(16 KiB → 4 KiB), the Rubber Ducky script (16 KiB → 4 KiB) and the text editor
(16 KiB → 8 KiB).

## Flash it

1. Flash the launcher update first (`update.uf2`, 1.2.5 or newer). The new
   launcher knows how to detect and start the module.
2. Copy `wifi_module.uf2` to the SD card root.
3. On the PicoPad: **LOAD** from SD → `wifi_module.uf2`.
4. In the flash dialog press **Y** (raw preset), then **A** on FLASH.

The module replaces whatever game was in the game area. Flashing a game later
erases it; flash the module again to get WiFi back. The launcher's PLAY option
detects the module and refuses to jump into it.

## WiFi credentials

The launcher reads `/wifi.cfg` from the SD card at boot and sends it to the
module:

```
ssid=MyNetwork
pass=MyPassword
```

Over USB you can also send (Companion Link console, target PicoPad):

```
WIFI CONNECT MyNetwork|MyPassword
WIFI STATUS
WIFI SCAN
```

Credentials are kept in RAM. The SD file is the persistent source of truth;
there is no internal flash write from this module, so it can never stall the
launcher.

## Settings > WIFI

The launcher's **SETTINGS → WIFI** menu now drives this module when it is
installed:

- **Status** — link state, SSID, IP, RSSI, TCP port
- **Scan networks** — live scan, pick a network, enter the password
- **Connect manually** — SSID + password
- **Connect saved** — uses `/wifi.cnf`
- **Disconnect** — leaves the network (saved credentials are kept)

Credentials are read from the existing ESP32 file `/wifi.cnf` (two lines:
SSID, password), or `/wifi.cfg` (`ssid=` / `pass=`). The menu writes
`/wifi.cnf`, so ESP32 and native WiFi share the same saved network.

If the module is not installed, the menu falls back to the old ESP32 bridge
screens.

## What works

- Launcher and WiFi at the same time; menu stays responsive.
- TCP 2323 with `PING`, `INFO`, `STATUS`, `WIFI STATUS/SCAN/CONNECT/CLEAR`,
  `LINK STATUS`, `HELP`.
- The launcher's WiFi icon turns on when the link is up.
- `STATUS` over the launcher's USB Companion Link reports the WiFi link.
- USB Companion Link keeps working; `WIFI ...` commands are forwarded to the
  module and replies are relayed back.

## Current limits

- `APPS` / `THEMES` / `THEME ...` over the module's TCP connection return
  empty/`ERR launcher`: those live on core 0 and need a cross-core RPC that is
  not implemented yet. Use the USB Companion Link for SD/theme work.
- The module has no SD/FAT, so it cannot read `/wifi.cfg` itself.
- Internal flash writes (settings save, game flash, launcher update) park
  core 1 in a RAM loop; WiFi pauses for the duration and resumes afterwards
  where applicable.
- Battery: GP29 is the radio's SPI clock while the module runs. The launcher
  samples the battery once before starting the module, caches it, and every UI
  battery read uses that cache. The module publishes the cached value in
  `STATUS`. It is not a live reading.

## Build

```sh
cd wifi_module
PICO_SDK_PATH=/usr/share/pico-sdk ./build.sh
```

Output: `dist/PICODECK_wifimod_<version>.uf2` (~570 KB).
