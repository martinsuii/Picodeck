# Themes

Themes change the boot splash, the screensaver and the launcher menus without
recompiling anything. Drop `.thm` or `.ini` files into `/themes/` on the SD
card, then pick one under **SETTINGS → Themes**. The choice is saved in
`/themes/active.cfg`, so it survives a power cycle. Pick `Default` there to go
back to the built-in look.

`showcase.thm` in this folder exercises most of the engine — copy it and
delete what you don't need.

## File syntax

One `key=value` per line. Blank lines and lines starting with `#` or `;` are
ignored. A trailing ` ; comment` is also ignored (except on mascot art lines).
Keys and named colours are case-insensitive. Whitespace is trimmed around keys
and values; art lines (`duck1`..`duck4`) keep everything after `=`.

Colours accept:

| Form | Example |
| --- | --- |
| `RRGGBB` | `35E0FF` |
| `#RRGGBB` | `#35E0FF` |
| `RGB` shorthand | `F0C` (= `FF00CC`) |
| `0xRRGGBB` | `0x35E0FF` |
| colour name | `cyan` |
| variable | `$neon` |

Names: `black white gray ltgray dkgray red green blue yellow cyan magenta
orange purple pink brown`.

Integers accept decimal and `0x` hex.

## Colour variables

Declare a colour once and reuse it anywhere a colour is accepted:

```ini
set ink   = 0A0F1E
set neon  = 35E0FF
menu_bg   = $ink
menu_title = $neon
boot      = text,-1,40,PICODECK,$neon,2,1
```

Declare variables **before** the lines that use them. Up to 16 variables per
theme, names up to 11 characters.

## Global keys

| Key | Meaning |
| --- | --- |
| `bg` | boot / screensaver background |
| `duck_color` | mascot colour |
| `duck1`..`duck4` | four-line ASCII mascot, taken literally after `=` |
| `accent` | secondary highlight (loader-warning text) |
| `ok` / `warn` / `err` | status palette: SD / USB / WiFi icons, battery, live values |
| `menu_bg` | background of the home + submenu screens |
| `menu_title` / `menu_text` / `menu_sel` / `menu_hl` / `menu_dim` | menu palette |
| `home_title1` / `home_title2` | the two big home-screen title lines |
| `home_time` / `home_time_color` | show and colour the top-right clock (`0`/`1`) |
| `home_time_x` / `home_time_y` | move the clock (negative = default position) |
| `home_weather` / `home_weather_color` | show and colour the weather text from the ESP32 bridge |

## Menu layout

| Key | Default | Meaning |
| --- | --- | --- |
| `menu_title_scale` | `2` | `1` or `2` |
| `menu_title_y` | `16` | first home-title line |
| `menu_title2_y` | `36` | second home-title line |
| `menu_sub_title_y` | `30` | submenu title (TOOLS, FUN, SETTINGS…) |
| `menu_divider_y` | `64` | divider under the title |
| `menu_start_y` | `80` | first row |
| `menu_line_h` | `20` | row height (8..64) |
| `menu_footer_y` | `210` | footer text baseline |
| `menu_sel_style` | `0` | `0` bar + arrow, `1` arrow, `2` inverted bar, `3` colour only |
| `menu_show_footer` | `1` | show the footer / help line |
| `menu_show_status` | `1` | show SD / USB / WiFi icons and battery |
| `menu_help` | `A: Open   X: Back` | submenu footer hint |

The number of visible rows is calculated from `menu_start_y`, `menu_line_h` and
the footer, so a theme with taller rows automatically gets more pages instead
of drawing off-screen.

## Timing and animation

| Key | Default | Meaning |
| --- | --- | --- |
| `boot_ms` | `2000` | boot splash duration in ms (100..60000) |
| `boot_show_version` | `1` | draw `vX.Y.Z` in the bottom-left of the splash |
| `saver_timeout` | `30` | idle seconds before the screensaver (0 = never) |
| `saver_speed` | `4` | mascot scroll step in px per frame |
| `saver_backlight` | `255` | backlight while saving, 0..255 (`255` = don't change) |
| `boot_append` | `0` | `1` = keep the built-in boot elements and append yours |
| `saver_append` | `0` | `1` = keep the built-in saver elements and append yours |

By default the first `boot=` / `saver=` line **replaces** the built-in list, so
a theme only draws what it asks for. Set `boot_append=1` or `saver_append=1`
anywhere in the file to draw on top of the defaults instead.

## Elements

Elements are drawn in file order. `boot=` elements appear on the boot splash,
`saver=` elements on the screensaver. Both lists support every type below.
Coordinates are pixels from the top-left (320×240 screen). A negative `x`
centres the element horizontally.

| Element | Arguments |
| --- | --- |
| `text` | `x,y,string,colour[,scale[,align]]` |
| `blink` | `x,y,string,colour,period_ms[,scale[,align]]` |
| `marquee` | `x,y,string,colour[,step_ms[,width]]` — scrolls right-to-left |
| `time` | `x,y,colour[,scale[,align]]` — local time from the ESP32 bridge |
| `weather` | `x,y[,colour]` |
| `duck` | `x,y` — animated mascot (moves on the screensaver) |
| `bar` | `x,y,w,h,colour` — progress bar (fills with boot time) |
| `pct` | `x,y[,colour[,align]]` — boot percentage |
| `spinner` | `x,y,colour` |
| `status` | `x,y` — USB / SD / battery block |
| `usb` | `x,y` — USB state text |
| `sd` | `x,y` — SD state text |
| `battery` | `x,y` — battery percentage + voltage |
| `rect` | `x,y,w,h,colour` — filled rectangle |
| `frame` | `x,y,w,h,colour[,thickness]` |
| `line` | `x1,y1,x2,y2,colour[,thickness]` |
| `circle` | `x,y,r,colour` — outline |
| `disc` | `x,y,r,colour` — filled |

`align` accepts `0`/`left`, `1`/`centre`/`center` or `2`/`right`. `scale` is
`1` or `2` (2 = double-size font).

Example boot splash:

```ini
boot_ms = 2400
boot = frame,8,8,304,224,$line,1
boot = text,-1,52,PICODECK,$cyan,2,1
boot = text,-1,78,LAUNCHER,$amber,2,1
boot = bar,60,140,200,14,$mint
boot = pct,266,142,$text
boot = spinner,284,142,$cyan
boot = blink,20,222,READY,$mint,600,1,0
```

Example screensaver:

```ini
saver_timeout   = 45
saver_backlight = 60
saver = frame,6,6,308,228,$line,1
saver = text,-1,14,PICODECK,$cyan,1,1
saver = duck,0,54
saver = status,40,126
saver = time,-1,186,$cyan,2,1
saver = weather,-1,212,$amber,1,1
saver = marquee,20,226,insert coin - insert coin,$dim,120,260
```

## Limits

- theme file: up to 4095 bytes
- up to 24 `boot=` and 24 `saver=` elements
- element text: up to 63 characters
- up to 16 colour variables
- mascot: 4 lines of up to 47 characters
