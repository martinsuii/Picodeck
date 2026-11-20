# PicoPad custom apps

Apps are normal PicoLibSDK programs that live on the SD card and are launched
from the launcher's **APPS** menu. They run while the native WiFi module keeps
running on core 1.

## Package layout

```
/apps/
  apps.cfg                  installed ids (managed by the launcher)
  hello/
    app.ini                 name=Hello
                            bin=app.bin
                            version=1.0
    app.bin                 built by ./build.sh
    config.ini              created on install, use AppConfig*
    data/                   optional, your files and saves
```

The launcher copies `app.bin` into a 252 KB flash strip when you launch it.
If the SD file has not changed, the copy is skipped.

## Build

From the repo root:

```bash
source setup_env.sh          # sets PICO_ROOT_PATH
cd apps_sdk
./build.sh examples/hello
```

`./build.sh` compiles every `.c` / `.cpp` in the app folder, links it at
`0x100A0000` with `app.ld`, and writes `<folder>/app.bin`.

Copy the whole app folder to `/apps` on the SD card, then on the PicoPad:

**APPS -> App Store -> From SD card -> <app> -> Install**

## Writing an app

```c
#include "include.h"     // PicoLibSDK + app API

void AppMain(void)
{
    DrawClear();
    DrawText2("Hello", 10, 10, COL_YELLOW);
    DrawText(AppWifiIp(), 12, 50, COL_WHITE);
    DispUpdate();

    while (1) {
        if (KeyGet() == KEY_X) return;   // return = back to the launcher
        WaitMs(30);
    }
}
```

Everything the built-in tools use is available: `DrawText`, `DrawRect`,
`DispUpdate`, `KeyGet`, sound, `FileOpen`/`FindOpen`, `WaitMs`, ...

### API

System:
- `AppExit()` - back to the launcher menu
- `AppReboot()` - restart
- `AppRecovery()` - reboot into the recovery menu
- `AppBootsel()` - reboot to the `RPI-RP2` USB drive
- `AppBsod(title, line1, line2, code)` - themed crash screen
- `AppBatteryMv()`, `AppId()`

Config / data:
- `AppConfigRead(key, buf, max)` / `AppConfigWrite(key, value)`
- `AppDataPath()` -> `/apps/<id>/data`

WiFi (native module on core 1):
- `AppWifiPresent()`, `AppWifiReady()`, `AppWifiLinkUp()`
- `AppWifiIp()`, `AppWifiRssi()`, `AppWifiSsid()`
- `AppWifiScan()`, `AppWifiConnect()`, `AppWifiDisconnect()`
- `AppHttpGet(url, buf, max)` - plain HTTP only (no TLS)

## Rules for apps

- **Do not reset peripherals, change clocks, or touch core 1.** The WiFi
  module owns the radio and its PIO/SPI setup. The SDK runtime in this folder
  already skips `RuntimeInit`/`DeviceInit` for that reason.
- **Do not read the battery ADC (GP29).** It is the CYW43 SPI clock. Use
  `AppBatteryMv()` (last launcher reading).
- Do not call PicoLibSDK `RuntimeInit()`, `DeviceInit()`, `BatInit()`, or
  `RuntimeTerm()`.
- Flash is shared with the launcher. Use the API, not raw flash calls.
- A hard fault shows the app BSOD. A hang needs a RESET press.

## How the flash strip is protected

- The strip is at `0x100A0000..0x100DF000`, the launcher starts at `0x100E0000`.
- Games/modules flashed through **LOAD** are capped below `0x100A0000`, so
  installed app code survives a game or WiFi module flash.
- Installing an app pauses core 1, erases only the sectors it needs, verifies
  the write, and resumes.
- boot2, recovery, the launcher and the WiFi module are never touched.

## Single-file packages (.pap)

The PicoPad's FAT layer only understands 8.3 names, so packages use the
3-character `.pap` extension (the container magic is `PAPP`). Pack an app
folder after building it:

```bash
python3 pack.py examples/hello
```

Copy `hello.pap` to `/apps` on the SD, then install it from
**APPS -> App Store -> From SD card**. The package is extracted to
`/apps/<NAME>/` and registered like a folder app.

Rules:
- The package filename base must be 8 chars or less (`WIFISCAN.PAP`).
- Files inside must be 8.3 (`APP.BIN`, `APP.INI`, `ICON.BMP`, ...).
- `config.ini` and `data/` are never packed.

## Open with app

Add a comma-separated extension list to `app.ini`:

```
open=txt,md
```

Now selecting a matching file in the file manager shows an **Open with**
chooser. The full path is passed to the app:

```c
const char* path = AppArgPath();
```

## Theme colours

Apps can match the active launcher theme:

```c
u16 bg   = AppTheme(APP_TH_BG);
u16 text = AppTheme(APP_TH_TEXT);
const char* name = AppThemeName();
```

Available indices: `APP_TH_BG`, `APP_TH_PANEL`, `APP_TH_TITLE`,
`APP_TH_TEXT`, `APP_TH_DIM`, `APP_TH_ACCENT`, `APP_TH_OK`, `APP_TH_WARN`,
`APP_TH_ERR`, `APP_TH_SEL`, `APP_TH_HL`.

## Raw TCP

Requires WiFi module 1.2.18 or newer. Two stop-and-wait sockets, 1 KB RX and
1 KB TX each. Do not use HTTP and sockets at the same time.

```c
int s = AppNetOpen("example.com", 80);
while (AppNetState(s) == 1 || AppNetState(s) == 2) WaitMs(20);
if (AppNetState(s) == 3) {
    AppNetSend(s, "hello", 5);
    char buf[256];
    int n = AppNetRecv(s, buf, sizeof(buf));
}
AppNetClose(s);
```

`AppNetRecv` returns 0 when nothing is available and drops bytes beyond
`max`. `AppNetFeatures()` returns the module feature bits.

## Update index

Put `/apps/index.txt` on the SD:

```
# id|version|size|url|name
hello|1.1|9394|hello.pap|Hello
```

The APPS list appends ` (update)` when the index version is newer than the
installed `app.ini` version.

## Online store (companion)

The PicoPad has no TLS. The companion fetches HTTPS and streams packages over
USB:

1. Start the demo catalogue: `companion/store/serve.sh`
2. Start the companion with the local URL:
   `STORE_URL=http://127.0.0.1:8811/index.txt node companion/server.js`
3. On the PicoPad: **APPS -> App Store -> From internet -> A**.

The default `STORE_URL` is `https://assets.msuii.dev/picopadapps/index.txt`.
Use an HTTP mirror or set `STORE_URL` to a working HTTPS endpoint.
