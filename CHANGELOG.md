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

## v0.9.2

- Home Assistant diagnostics, so the monitor no longer needs a laptop on
  USB to log problems:
  - Stove/Flue probe glitches: every bad probe read since boot, including
    one-second blips too short to show a fault on the screen.
  - Stove/Flue probe last glitch: what the probe chip reported (open
    circuit, voltage, out of range, board temperature, chip not answering).
  - Free memory, lowest free memory since boot, largest free block.
- Dashboard: these on History → Monitor health, with 48-hour graphs of
  probe glitches and free memory.
- Console `show` lists glitches and the last glitch per probe.

## v0.9.1

- Fix: the monitor could stay on Wi-Fi but stop answering anything (web
  page, Home Assistant), until it was power-cycled. The weather fetch's
  HTTPS took internal RAM down to 2 KB. HTTPS now uses PSRAM; the lowest
  point at boot is now ~48 KB.
- Wi-Fi watchdog: the monitor pings the router every 30s. If the router
  stops answering while Wi-Fi still says connected, it reconnects, and
  after 10 min restarts itself (shown as "network stuck restart").
- Console `mem` shows how long the router has been silent.

## v0.9.0

- Home Assistant over MQTT. The monitor shows up as a **Stove Monitor**
  device: temperatures, rates, outside temperature, alarm states, probe
  faults, a Silence button, and **every setting** as a switch or number.
  Changes from HA are validated like the touchscreen's.
- The broker (address, port, login) is entered on the web page and stored only
  on the monitor. MQTT stays off until it's set, and alarms never depend on it.
- **Burn zone** sensor (Too cool, Ideal burn, Running hot, Chimney too cool),
  decided the same way as on the touchscreen.
- `docs/ha-dashboard.yaml`: the Stove dashboard (gauges, burn zone, alarm
  banner, quick controls, every setting, history). `docs/home-assistant.md`:
  setup and phone notifications (chimney fire, critical, probe fault,
  offline, warning, reload, Silence from the notification).
- Memory: the boot-time weather fetch and the MQTT connect take turns, so a
  restart with the broker running no longer exhausts RAM and drops Wi-Fi.
  If internal RAM ever stays nearly exhausted for a minute, the monitor
  restarts itself and reports "low memory restart".

## v0.8.0 - 2026-10-17

- **Flip screen:** a new Settings → Screen tab, also on the web page, turns
  the display 180° so the unit can be mounted with its cable on the other
  side. Touch flips with it. Brightness moved to this tab too.
- **Steadier rate of change:** the °F/min figure is now the slope of a
  straight line fitted through the last 60 seconds of readings, instead of
  newest minus oldest. A single jumpy reading no longer throws it off. The
  chimney-fire rate alarm uses the same, steadier figure.

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
