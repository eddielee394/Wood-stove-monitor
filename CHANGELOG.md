# Changelog

Firmware for the wood stove monitor (WT32-SC01 Plus). Versions follow
MAJOR.MINOR.PATCH: PATCH for fixes, MINOR for new features, MAJOR for changes
that break compatibility (e.g. settings that don't carry over). The firmware
stays below 1.0 until it has been mounted and its alarm thresholds checked
against real burns.

Each release on GitHub has two files:
- `stove-monitor-vX.Y.Z.bin`: install over Wi-Fi; settings are kept.
- `stove-monitor-vX.Y.Z-full.bin`: USB recovery image, flashed at 0x0. It
  erases settings, Wi-Fi and the OTA password.

## v0.7.0 - 2026-10-16

- **Reload temperature on the touchscreen:** Settings → Alerts has a new
  "Reload below" row with − / + buttons (steps of 25°F). It always stays
  below the STOVE warning. It could already be set from the web page.
- **Cleaner dashboard:** the "collar pipe" and "chimney exit" labels under
  STOVE and FLUE are gone, to free up space.

## v0.6.0 - 2026-10-13

First versioned release: everything built so far (phases 0-5) plus the
outside temperature.

- **Readings:** STOVE (collar pipe) and FLUE (chimney exit) thermocouples,
  smoothed, with rate of change and probe-fault detection (open, shorted,
  missing chip). A faulted probe never shows a temperature.
- **Alarms, on the device with no network needed:** warning and critical per
  probe, a FLUE rate-of-rise "possible chimney fire" alarm (above 500°F), a
  reload alert, and probe-fault alarms, each with its own beep pattern.
  Silence covers only the alarms active at that moment, and re-arms after a
  timeout. Quiet mode mutes Warning and Reload; Critical and faults always
  sound.
- **Touchscreen:** dashboard with burn-zone bar and ideal-burn indicator,
  full-screen alarm screens, and settings (thresholds, alerts, brightness,
  Wi-Fi setup with an on-screen keyboard).
- **Network:** Wi-Fi with automatic reconnect; a web page with live readings,
  silence and every setting; firmware updates over Wi-Fi (password
  protected).
- **Outside temperature:** from Open-Meteo for a location set on the web
  page, shown with a weather icon in the top bar.
- **Version:** shown on the Settings screen, in the web page footer, in
  `/api/status` and in the console's `show` command.
