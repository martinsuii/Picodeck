> **Standalone fallback.** This is the older "WiFi mode as a game" build.
> The launcher-integrated version lives in
> [`../wifi_module/`](../wifi_module/README.md) and keeps the menu running
> while WiFi is active. Use this one only if you want WiFi as a separate boot
> target.

# WiFi Link (Pico W)

A standalone Pico W app that replaces a game in the game flash area and gives
the PicoPad native WiFi — no ESP32 bridge needed.

It is built with the **Raspberry Pi Pico SDK**, not PicoLibSDK, because that is
where the CYW43439 driver and lwIP live. The launcher's game region is ~896 KiB,
so the ~300 KiB image fits with plenty of room. RAM is fine too: the launcher
isn't running while this app is, so lwIP gets the whole 256 KiB.

## What it does

- TCP server on **port 2323** speaking the same line protocol as Companion Link
- USB CDC on the same protocol (over the cable)
- WiFi station mode, credentials saved in flash
- status screen on the ST7789; **X** reboots back to the launcher

The protocol is documented in [`../companion/protocol.md`](../companion/protocol.md).
The WiFi-specific commands are:

```
WIFI STATUS
WIFI SCAN
WIFI CONNECT <ssid>|<pass>
WIFI CLEAR
```

`WIFI CONNECT` saves the credentials to the last flash sector of the game
region (`0x100DE000`) and starts connecting.

The usual Companion Link commands work too: `PING`, `INFO`, `STATUS`,
`REBOOT`, `BOOTSEL`, `HELP`, plus `LINK STATUS/START/STOP` for compatibility.
`APPS`/`THEMES` report `0` for now and `THEME ...` returns `ERR no sd`, because
v1 has no FAT driver in this image. The companion's SD tab is unaffected (it
reads the card through the computer/phone reader).

## Build

Needs the Pico SDK and a GCC ARM toolchain:

```sh
cd wifi_link
PICO_SDK_PATH=/usr/share/pico-sdk ./build.sh
```

Output: `wifi_link/dist/PICODECK_wifi_1.2.5.uf2` (~600 KB UF2,
~300 KiB of code, ~36 KiB BSS).

## Flash it

1. Copy the UF2 to the SD card root.
2. On the PicoPad: **LOAD** from SD → pick the UF2.
3. In the flash dialog, **Y** selects the raw preset (no PicoDeck patches), then
   **A** on FLASH. The plain preset is fine too; the PicoDeck patch scans simply
   find nothing to patch.
4. PLAY starts WiFi Link. To get the game area back, flash a game UF2.

`Preserve launcher` must stay on — that keeps boot2 and the launcher intact.

## First run

Open the companion app, connect to the PicoPad's IP on port 2323, and send:

```
WIFI CONNECT MyNetwork|MyPassword
```

Or over USB. The screen then shows the IP. On the next boot it reconnects
automatically.

## Hardware facts that matter

- The radio is the Pico W's **CYW43439**; the firmware blob alone is 220 KiB.
- The battery is on **GP29**, which the CYW43 uses for its SPI clock. The app
  samples the battery once at boot, before `cyw43_arch_init()`.
- Display is SPI0 on GP16–21, buttons on GP2–9, SD is SPI1 on GP10–13.

## Why not inside the launcher?

The launcher is built with PicoLibSDK, which has no WiFi support at all, and
its 108 KiB partition has ~10 KiB free. Adding the driver there would need a
partition redesign. Running as a game-area app sidesteps both problems.
