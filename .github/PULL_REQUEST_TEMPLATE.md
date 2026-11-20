## What

<!-- What does this change do? -->

## Why

<!-- The problem it solves, or the issue it closes. -->

## Where

- [ ] Launcher / recovery
- [ ] WiFi module
- [ ] App SDK / apps
- [ ] App store / server
- [ ] Companion (desktop / Android / web)
- [ ] Docs / templates

## Tested on

- PicoDeck version:
- Hardware: PicoPad Arcade / Pico W:
- WiFi module version:
- SD card:

## Checklist

- [ ] Built with `-DLAUNCHER_RELEASE` and it fits the launcher region.
- [ ] boot2 → recovery → launcher still boots.
- [ ] Game flash still writes boot2 first and verifies it.
- [ ] Launcher update still writes recovery first.
- [ ] App install only touches the app slot/record.
- [ ] App API address changes rebuild every app (`app_shared.h`, `apps_sdk/app.ld`).
- [ ] No build outputs committed (`.uf2`, `.bin`, `.elf`, `build/`, `.pap`, `dist/`).
- [ ] Docs updated if behaviour changed.

## Notes

<!-- Anything risky, untested, or needing a hardware test. -->
