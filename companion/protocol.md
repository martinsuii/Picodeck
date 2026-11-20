# Companion Link protocol

The PicoPad launcher runs **Companion Link** as a normal app from the TOOLS
menu. It accepts one line per command and answers with one or more lines. The
same protocol runs over two transports:

```
USB:   computer/phone ──USB CDC──> PicoPad
WiFi:  phone ──TCP 2323──> ESP32 ──UART "@cmd"──> PicoPad
       PicoPad ──UART "@reply"──> ESP32 ──TCP──> phone
```

Lines end with `\n`. A trailing `\r` is ignored. Text is ASCII; names may
contain spaces. `Companion Link` limits a command line to 160 bytes. The ESP32
bridge limits a line to 512 bytes and supports two TCP clients.

The USB CDC interface is a normal serial port (115200 8N1 is a safe setting,
though USB CDC ignores the baud rate). On Linux it appears as
`/dev/ttyACM*`.

---

## USB transport

No framing prefix is used. Everything written to the CDC port goes to the
PicoPad, and every reply comes back on the CDC port.

```
PING\n              ->  PONG
```

## WiFi transport (ESP32 bridge)

The ESP32 bridge owns TCP port 2323. On connect it prints one greeting:

```
PICOPAD COMPANION
```

After that:

- A **plain TCP line** is forwarded to the PicoPad UART as `@<line>`.
- A PicoPad line starting with `@` is forwarded to all TCP clients with the
  `@` removed.
- A TCP line starting with `!` is handled by the ESP32 itself and is never
  forwarded.
- Non-`@` UART lines stay in the ESP32 command protocol. This is what keeps
  the two protocols from interpreting each other's commands.

Example:

```
phone:   PING
ESP32:   @PING
PicoPad: @PONG
phone:   PONG
```

The PicoPad starts the server when Companion Link opens and the ESP32 reports
a connected WiFi network. It sends `LINK START 2323` to the ESP32. The same can
be done by hand over the ESP32 console.

### ESP32-local commands (`!` prefix over TCP)

| Command | Reply |
| --- | --- |
| `!LINK STATUS` | `LINK ON <port> 1` |
| `!LINK STOP` | `OK stopping`, then the server closes and sends `BYE` to clients |
| `!INFO` | `ESP32 <chip> <ip>` |
| anything else | `ERR only !LINK / !INFO` |

### ESP32 console commands (UART/SD-side, not over the link)

| Command | Reply |
| --- | --- |
| `LINK START [port]` | `OK <ip>:<port>` (default 2323) |
| `LINK STOP` | `OK` |
| `LINK STATUS` | `LINK ON|OFF <port> <clients>` |

`PING` / `WIFI STATUS` are also used by the PicoPad during the handshake.

---

## PicoPad commands

### `PING`

```
PING
PONG
```

### `INFO`

```
INFO
INFO 1.2.9 1MB
USB mounted
```

The second line is `USB mounted` while a CDC host is connected, otherwise
`USB idle`. A 2 MB build reports `2MB`.

### `STATUS`

```
STATUS <version> <millivolts> <percent> <sd> <usb> <esp> <theme>
```

Example:

```
STATUS 1.2.9 4021 87 1 1 0 neon.thm
```

- `sd` – `1` when the SD card is mounted
- `usb` – `1` when USB CDC is mounted
- `esp` – `1` when the ESP32 is detected on the UART
- `theme` – active theme file name, or `(default)`

### `APPS`

```
APPS 3
APP 0 Games
APP 1 Tools
APP 2 Companion Link
END
```

`APPS <n>` is followed by exactly `n` `APP <id> <label>` lines and `END`.
Apps that own the USB port are not listed while the link is using it.

### `LAUNCH <id>`

```
LAUNCH 0
OK launching
```

`LAUNCH` with an unknown id returns `ERR app id`. The launcher exits
Companion Link and starts the selected app.

### `THEMES`

```
THEMES 2
THEME neon.thm
THEME showcase.thm
END
```

`THEMES <n>` is followed by `n` `THEME <file>` lines and `END`. `n` is `0`
when no SD card is mounted.

### `THEME SET <file>`

```
THEME SET neon.thm
OK
```

Writes `themes/active.cfg`. Returns `ERR no sd` without a card. The file name
is passed through to the theme loader; it is not validated here.

### `THEME PUT <file>` (stateful upload)

```
THEME PUT mytheme.thm
READY
bg=000000
menu_title=FFCC00
.
OK 37
```

After `READY`, every line is written verbatim into `/themes/<file>` until a
single `.` line ends the upload. Empty lines are written as empty lines.

- `OK <bytes>` – bytes written, including newlines
- `ERR name` – empty, leading `.` or invalid characters; allowed characters
  are `A-Z a-z 0-9 _ - . space` (max 63)
- `ERR no sd` – no card mounted
- `ERR create` – cannot create the file
- `ERR too big` – more than 8192 bytes of upload data

The theme engine still rejects files larger than 4095 bytes at load time, so
keep uploads below 4 KB. The active theme is reloaded after a successful
upload.

### `REBOOT`

```
REBOOT
OK
```

Replies first, then restarts the launcher.

### `BOOTSEL`

```
BOOTSEL
OK
```

Replies first, then reboots into the RP2040 bootloader so the device appears
as a USB mass-storage drive.

### `HELP` / `?`

Returns the short built-in command list.

### Unknown command

```
ERR unknown
```

---

## Client behaviour

The companion sends `PING` and `STATUS` after connecting. It keeps the reply
log in memory (last 400 lines) and uses `STATUS`, `APPS` and `THEMES` to fill
the Device tab. Sent lines are echoed in the log with a leading `> `.

Over WiFi, the app's **ESP32** target prefixes `!` automatically; the
**PicoPad** target stays plain. Over USB both targets go to the PicoPad.

The Android app and desktop app speak the same protocol; the Android app
implements the transport directly in Java (USB CDC or TCP), while the desktop
app uses Node serial/TCP and exposes the same controls through its HTTP API.

---

## HTTP API (desktop/server UI)

The local server exposes JSON endpoints under `/api` for the browser UI:

| Endpoint | Purpose |
| --- | --- |
| `GET /api/state` | SD, device and config state |
| `GET/POST /api/config` | saved SD path, ESP host/port, serial settings |
| `GET /api/sd` | SD card info |
| `GET /api/sd/list?path=` | directory listing |
| `GET /api/sd/download?path=` | file download |
| `POST /api/sd/upload?path=` | file upload (raw body) |
| `POST /api/sd/mkdir`, `/delete`, `/rename` | file management |
| `GET /api/sd/backup` | `.tar.gz` backup |
| `POST /api/sd/restore` | restore a `.tar.gz` |
| `GET /api/themes?source=sd|repo` | theme list |
| `GET/PUT /api/themes/file?source=&name=` | read/write a theme |
| `POST /api/themes/active` | set the active theme |
| `POST /api/themes/parse` | parse theme text |
| `POST /api/uf2/inspect` | validate a UF2 body |
| `POST /api/uf2/stage` | write `update.uf2` to the card |
| `GET /api/uf2/local` | latest build artifact, when present |
| `GET /api/device/ports` | serial/USB port list |
| `GET /api/device/state` | connection state |
| `POST /api/device/connect` | connect over USB or WiFi |
| `POST /api/device/disconnect` | close the link |
| `POST /api/device/send` | send `{line, target}` |
| `GET /api/device/events` | SSE `state` and `line` events |

The Android app does not run this server. It implements the same routes in
`web/local-api.js` on top of a Java bridge (`window.PicoAndroid`), so the UI
and theme/UF2 code are shared unchanged.
