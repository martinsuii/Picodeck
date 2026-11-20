# Making PicoDeck apps

PicoDeck apps are ordinary PicoLibSDK programs that live on the SD card. They
get the same display, keys, sound and SD functions as the built-in tools, plus
a small API for WiFi, config, themes and system actions.

Apps run while the native WiFi module keeps running on core 1. Leaving an app
returns to the launcher without rebooting.

## Quick start

```bash
cd apps_sdk
cp -r examples/hello examples/myapp
# edit examples/myapp/main.c and examples/myapp/app.ini
./build.sh examples/myapp
python3 pack.py examples/myapp
```

Copy `examples/myapp` (or `myapp.pap`) to `/Apps` on the SD card. On the
PicoPad: **APPS → App Store → From SD card → myapp → Install**.

## App folder

```
/apps/myapp/
  app.ini
  app.bin
  config.ini      (created on install)
  data/           (your files, optional)
```

`app.ini`:

```ini
name=My App
bin=app.bin
version=1.0
author=you
open=txt,md
```

- `name` — shown in APPS.
- `bin` — the compiled binary (default `app.bin`).
- `version` — used by the update index.
- `open` — optional comma-separated extensions the app can open from the file
  manager.
- `author` — optional, free text.

Keep the folder name and file names **8.3-safe**. The PicoPad's FAT layer does
not support long names. The `.pap` packer enforces this.

## Code

```c
#include "include.h"   // PicoLibSDK + App API

void AppMain(void)
{
    int frames = 0;

    while (1)
    {
        DrawClear();
        DrawText2("My App", 10, 10, AppTheme(APP_TH_TITLE));
        DrawText(AppWifiIp(), 12, 48, AppTheme(APP_TH_TEXT));
        DrawText("A: action   X: exit", 12, 210, AppTheme(APP_TH_DIM));

        DrawRect(12 + (frames % 120), 90, 12, 12, AppTheme(APP_TH_ACCENT));
        frames++;
        DispUpdate();

        u8 k = KeyGet();
        if (k == KEY_X) return;             // returns to the launcher
        if (k == KEY_A) { /* ... */ }

        WaitMs(30);
    }
}
```

The SDK provides `main()` and the runtime; define `void AppMain(void)`. Return
from it (or call `AppExit()`) to go back to the launcher.

## What you can use

PicoLibSDK's normal API is available, including:

- `DrawText`, `DrawText2`, `DrawRect`, `DrawFrame`, `DrawCircle`, `DrawClear`,
  `DispUpdate`, `DispDirtyAll`, `SelFont8x8`
- `KeyGet`, `KeyPressed`, `KEY_*`
- `WaitMs`, `Time`
- `FileOpen/FileRead/FileWrite/FileCreate/FileDelete`, `FindOpen/FindNext`
- sound and the other SDK libraries compiled into the app

### Runtime rules (important)

The launcher already initialised the display, keys, SD and the WiFi module.
The app SDK handles this with a custom runtime. **Do not** call:

- `RuntimeInit()`
- `DeviceInit()`
- `BatInit()`
- `DispInit()` — re-running it resets SPI and can hard-fault
- `RuntimeTerm()`

If you need to refresh the display state, use `DispDirtyAll()` after drawing.
Do not touch core 1 or the radio pins (GP23/24/25/29); the module owns them.
Use `AppBatteryMv()` instead of reading the battery ADC.

## App API

### Entry and system

| Function | Purpose |
| --- | --- |
| `void AppMain(void)` | your app entry point |
| `AppExit()` | back to the launcher (no reboot) |
| `AppReboot()` | full restart |
| `AppRecovery()` | reboot into the recovery menu |
| `AppBootsel()` | reboot to the `RPI-RP2` USB drive |
| `AppBsod(title, line1, line2, code)` | themed crash screen |
| `AppBatteryMv()` | last battery reading from the launcher |
| `AppId()` | app folder id |
| `AppApiVersion()` | API version (1) |

### Config and data

```c
char value[48];
if (AppConfigRead("host", value, sizeof(value)) > 0) { /* ... */ }
AppConfigWrite("host", "example.com");   // 0 = ok
const char* data = AppDataPath();        // /apps/<id>/data
```

`config.ini` is per app and survives reinstall. `Clear data` in the app
manager deletes it and the contents of `data/`.

### Theme

```c
u16 bg    = AppTheme(APP_TH_BG);
u16 text  = AppTheme(APP_TH_TEXT);
u16 acc   = AppTheme(APP_TH_ACCENT);
const char* name = AppThemeName();
```

Indices: `APP_TH_BG`, `APP_TH_PANEL`, `APP_TH_TITLE`, `APP_TH_TEXT`,
`APP_TH_DIM`, `APP_TH_ACCENT`, `APP_TH_OK`, `APP_TH_WARN`, `APP_TH_ERR`,
`APP_TH_SEL`, `APP_TH_HL`.

The launcher passes its active theme colours in the app handoff. If no palette
is present the SDK returns sensible defaults.

### Open with app

Declare the extensions in `app.ini`:

```ini
open=txt,md
```

When the user selects a matching file in the file manager, a chooser appears.
The full path is available as:

```c
const char* path = AppArgPath();   // "" when launched normally
```

### WiFi

```c
if (AppWifiLinkUp())
{
    DrawText(AppWifiIp(), 10, 10, COL_WHITE);
    DrawText(AppWifiSsid(), 10, 26, COL_WHITE);

    AppWifiNet nets[16];
    int n = AppWifiScan(nets, 16, 10000);
    for (int i = 0; i < n; i++)
        DrawText(nets[i].ssid, 10, 44 + i * 12, COL_WHITE);
}
```

- `AppWifiPresent()` — module installed
- `AppWifiReady()` — module running
- `AppWifiLinkUp()` — connected with an IP
- `AppWifiIp()`, `AppWifiRssi()`, `AppWifiSsid()`
- `AppWifiScan(out, max, timeout_ms)` — returns count, -1 not ready, -2 timeout
- `AppWifiConnect(ssid, pass)`, `AppWifiDisconnect()`

### HTTP GET

```c
char body[1024];
int n = AppHttpGet("http://example.com/", body, sizeof(body));
if (n > 0)
{
    // n includes HTTP headers; find the blank line if you need the body
    char* p = strstr(body, "\r\n\r\n");
    if (p) { p += 4; /* use p */ }
}
```

- Plain HTTP only. `https://` is not supported by the module.
- The response is capped at the module's 4 KB network buffer.
- Do not use HTTP and raw TCP at the same time; they share the buffer.
- The URL must fit in the module's 160-character command line.

### Raw TCP

```c
int s = AppNetOpen("example.com", 80);
while (AppNetState(s) == 1 || AppNetState(s) == 2) WaitMs(20);

if (AppNetState(s) == 3)
{
    AppNetSend(s, "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n", 38);

    char buf[512];
    for (int i = 0; i < 100; i++)
    {
        int n = AppNetRecv(s, buf, sizeof(buf));
        if (n > 0) { /* use buf[0..n-1] */ }
        if (AppNetState(s) == 4) break;
        WaitMs(20);
    }
}
AppNetClose(s);
```

- 2 sockets, 1 KB RX + 1 KB TX each, stop-and-wait.
- `AppNetState`: 0 idle, 1 DNS, 2 connecting, 3 open, 4 closed, 5 error.
- `AppNetRecv` returns 0 when nothing is available and drops bytes above `max`.
- `AppNetSend` returns 0 on success, negative on error.
- `AppNetFeatures()` reports module support.

## Build

`apps_sdk/build.sh` compiles every `.c`/`.cpp` in the app folder, links it at
`0x100A0000` with `app.ld`, and writes `<folder>/app.bin`.

```bash
./build.sh examples/hello
```

The app image is limited to **252 KB**. App RAM ends at `0x20037800`; the
launcher handoff and the module's 4 KB network buffer sit above it.

### Package (`.pap`)

```bash
python3 pack.py examples/hello
```

- Container magic `PAPP`, uncompressed.
- Files must be 8.3 (`APP.BIN`, `APP.INI`, assets).
- The package file extension is `.pap` (3 characters) because the PicoPad's FAT
  cannot handle 4-character extensions.
- `config.ini` and `data/` are not packed.

## Crashes and debugging

- A hard fault or unhandled IRQ shows a full-screen BSOD with IRQ, PC, CFSR,
  HFSR and registers.
- Press **Y** to save `/crash/CRASHnnn.TXT` on the SD card.
- Press **A** to return to the launcher, **X** to restart.
- The BSOD uses direct display writes, so it works even if the app's runtime is
  broken.

For normal debugging, draw state on screen or write a file to the SD card.
There is no serial console in apps.

## Examples

- [`examples/hello`](../apps_sdk/examples/hello) — drawing, WiFi status, scan.
- [`examples/wifiscan`](../apps_sdk/examples/wifiscan) — status, scan, connect.
- [`examples/nettest`](../apps_sdk/examples/nettest) — raw TCP, HTTP-style GET.

## Distribution

For a local app, copy the folder to `/Apps`. For a single file, copy the
`.pap`. To publish in the online store, host the `.pap` and add it to the
catalogue index; see [Contributing → App store](contributing.md#app-store).

## Limits

- 252 KB code, 8.3 file names, 640 KB shared game/WiFi area.
- No TLS, no raw sockets beyond 2, 4 KB shared network buffer.
- No MPU/sandbox. A bad app can write flash; recovery and BOOTSEL are the
  safety net.
- The launcher snapshot for no-reboot exit writes ~227 KB to flash per launch.
