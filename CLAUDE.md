# Stove Monitor — Project Instructions for Claude Code

Custom firmware for a wood stove temperature monitor. One device: a WT32-SC01 Plus
touchscreen board reads two K-type thermocouples through two MAX31856 breakouts,
shows temperatures on its screen, sounds local alarms, serves a web page on the LAN,
and (later) integrates with Home Assistant over MQTT.

The owner is Eddie. He handles all physical work (soldering, wiring, looking at the
screen). You handle all code, builds, flashing, and debugging. When a step needs eyes
or hands on the hardware, stop and ask Eddie a specific question.

---

## 1. Hard rules (read first)

- **Never run `espefuse` or any command that burns eFuses.** Never enable Secure Boot
  or Flash Encryption. These are irreversible.
- **Never overwrite or delete `~/wt32_backup.bin`.** It is the only copy of the
  original vendor firmware (full 16MB image).
- Avoid `erase-flash` unless genuinely needed to recover; prefer normal uploads.
- **Safety logic lives on the device and must work with no Wi-Fi, no MQTT, no Home
  Assistant.** Network features are additions, never dependencies, for alarms.
- Never block the sensor/alarm path with network, UI, or file I/O work.
- Keep Wi-Fi credentials and other secrets out of git (see section 7).
- Commit working states to git frequently with clear messages. Each phase in
  section 9 should end in a commit.
- Before declaring a hardware-facing step done, ask Eddie to confirm what he sees
  (screen output, readings, buzzer). Do not assume.

---

## 2. Confirmed hardware facts

### Main board: Wireless-Tag WT32-SC01 Plus (board code ZX3D50CE08S-V15)
Verified with esptool on the actual unit:

| Item | Value |
|---|---|
| Chip | ESP32-S3 (QFN56), revision v0.2, dual core 240MHz |
| Flash | 16MB, quad |
| PSRAM | 2MB embedded, quad (AP_3v3) — NOT octal |
| USB | Native USB-Serial/JTAG (no USB-UART chip) |
| Secure Boot | Disabled |
| Flash Encryption | Disabled, no keys burned |
| MAC | 34:85:18:b2:a9:ac |

### Internal pin usage (from datasheet — do not reassign)

| Function | GPIO |
|---|---|
| LCD 8-bit parallel D0–D7 | 9, 46, 3, 8, 18, 17, 16, 15 |
| LCD WR | 47 |
| LCD RS (D/C) | 0 (also the BOOT strap pin) |
| LCD TE (frame sync) | 48 |
| LCD reset + touch reset (shared) | 4 |
| Backlight PWM (active high) | 45 |
| Touch I2C SDA / SCL / INT (FT6336, addr 0x38) | 6 / 5 / 7 |
| microSD CS / MOSI / CLK / MISO | 41 / 40 / 39 / 38 |
| I2S audio amp (NS4168) LRCK / BCLK / DOUT → SPK connector | 35 / 36 / 37 |
| RS-485 RXD / RTS / TXD (unused) | 1 / 2 / 42 |
| USB D−/D+ (do not use) | 19 / 20 |

Display: 3.5" 480×320, ST7796 controller. Use landscape rotation.

### I/O header (8-pin, MX1.25) — all six GPIOs are allocated

| Header pin | Signal | Use |
|---|---|---|
| 1 (marked +) | +5V | Power to both MAX31856 breakouts + buzzer |
| 2 (marked −) | GND | Common ground |
| 3 | GPIO10 | CS — stove probe MAX31856 |
| 4 | GPIO11 | SPI MOSI → SDI on both breakouts |
| 5 | GPIO12 | SPI SCK → SCK on both breakouts |
| 6 | GPIO13 | SPI MISO ← SDO on both breakouts |
| 7 | GPIO14 | CS — flue probe MAX31856 |
| 8 | GPIO21 | Buzzer |

GPIO10–13 are the ESP32-S3 native FSPI (SPI2) pins — use hardware SPI on that host.
Pin order is per datasheet; Eddie will verify +5V/GND with a multimeter before wiring.

### Thermocouple front end
- 2× Adafruit MAX31856 breakouts (product #3263), powered from 5V (onboard regulator
  and level shifting; logic is 3.3V-safe).
- Mounted in a separate passive junction box, connected by a short (<1m) Cat5 run.
- Probe placement (confirmed by Eddie): **STOVE** channel (CS GPIO10) is on the
  flue pipe just above the stove collar; **FLUE** channel (CS GPIO14) is at the top
  of the chimney, just outside the pipe. Double-wall pipe; Eddie verified little
  heat loss through the wall at the probe. Stove is catalytic. Keep the
  STOVE / FLUE labels.
- Probes: existing installed K-type thermocouples, 1.5m leads, labeled "K1.5M".
  Polarity unverified — if a reading drops when the tip is warmed, the leads are
  swapped (hardware fix, not software).
- Configure each chip: thermocouple type K, 60Hz noise filter (US mains),
  continuous conversion, fault detection enabled.
- SPI note: if the SD card is used later, run it on the other SPI host (SPI3) with
  its own pins (38–41). Do not share the FSPI bus between SD and the thermocouples.

### Buzzer
- Small magnetic buzzer already mounted inside the display case on GPIO21.
- **Verified in Phase 1:** it is an **active** buzzer that sounds when GPIO21 is
  driven **HIGH** (LOW is silent), so there is a driver between the pin and the
  coil; the earlier note about it being wired straight from +5V to GPIO21 was
  wrong. It also reproduces LEDC tones cleanly and is loudest around 2.5–3.5kHz.
- Driver: `src/buzzer.{h,cpp}`, LEDC channel 0 (timer 0; backlight uses channel 7 /
  timer 3), patterns stepped by an esp_timer so calls never block. Patterns
  approved by Eddie: Warning = three 2.7kHz beeps + 1s pause; Critical = rapid
  continuous 2.7kHz beeps; Fault = long 2.2kHz beeps; Chirp = single short beep.
- Future option: a small 8Ω speaker on the SPK connector via the I2S amp for louder,
  multi-tone alarms.

---

## 3. Development environment

- **Host:** macOS. Eddie's editor is PhpStorm (no C++ support); he may use CLion.
  Build and flash with the PlatformIO CLI regardless of editor.
- **Python venv:** `~/esptool-env` (activate with `source ~/esptool-env/bin/activate`).
  esptool v5.x is installed there — use hyphenated commands (`read-flash`,
  `write-flash`, `flash-id`, `get-security-info`).
- Install PlatformIO into the same venv if missing: `pip install platformio`.
- **Serial port:** was `/dev/cu.usbmodem2101`; it can change. Detect with
  `ls /dev/cu.usbmodem*`. The board must be plugged **directly** into the Mac —
  a USB hub previously prevented enumeration.
- Build/flash: `pio run -t upload --upload-port <port>`; monitor: `pio device monitor`.
- **Over Wi-Fi (preferred once installed):** `./tools/ota-upload.sh` builds and
  POSTs the firmware to `http://stove-monitor.local/update` (HTTP Basic auth,
  user `ota`). The password lives only in `~/.config/stove-monitor/ota_password`
  (600) and in the board's NVS (console `otapass <pw>`, never echoed); OTA is
  off until it is set. HTTP/TCP was chosen because **UDP and ICMP from Eddie's
  Mac to the LAN are blocked** (a network extension, likely VPN/security
  software): ArduinoOTA/espota and `ping` fail, TCP works. Use
  `dangerouslyDisableSandbox` for LAN commands from Claude's shell.
- **Versions and releases:** MAJOR.MINOR.PATCH, below 1.0 until the
  monitor is mounted and its thresholds are checked against real burns.
  `tools/version.py` (a PlatformIO pre-script) stamps each build from
  `git describe` (`v0.6.0`, `v0.6.0-3-gabc1234`, `...-dirty`) into
  `include/version_gen.h` (gitignored). The version shows on the Settings
  Stove/Flue tab, in the web footer, in `/api/status` and in console `show`.
  To release: add a `## vX.Y.Z` section to `CHANGELOG.md`, commit and push,
  then `tools/release.sh X.Y.Z [--install 192.168.1.98]`. It tags, builds,
  attaches the OTA `.bin` and a full USB recovery image (which erases NVS)
  to a GitHub Release, and can install the release over Wi-Fi. Ask Eddie
  before cutting a release.
- Console `show` prints the last reset reason (also `reset_reason` in
  `/api/status`) — check it after any unexpected restart.
- Restore original firmware if ever needed:
  `python -m esptool --port <port> write-flash 0 ~/wt32_backup.bin`

### Recovery if a flash leaves USB unresponsive
There is no BOOT button. Hold GPIO0 (BOOT pin on the 7-pin DEBUG connector) to GND,
tap RST, then release — the ROM bootloader always enumerates over USB. Walk Eddie
through this if needed; don't guess which DEBUG pin is which without checking the
datasheet.

---

## 4. Software stack

- PlatformIO, Arduino framework. Starter `platformio.ini` targets
  `espressif32@^6.9.0` with board `esp32-s3-devkitc-1` plus overrides for 16MB flash
  and quad PSRAM. Migrating to Arduino core 3.x (pioarduino platform) is acceptable
  if a library requires it — pin versions and document the change.
- **Display driver:** LovyanGFX with a manual config (in the starter `src/main.cpp`).
- **UI:** LVGL 9, with LovyanGFX as the flush/touch backend. Draw buffers in internal
  RAM or PSRAM as performance dictates.
- **Thermocouples:** Adafruit MAX31856 library on hardware SPI (FSPI, custom pins).
- **Settings:** NVS via `Preferences`.
- **Network:** WiFi STA, async web server for the local status page, MQTT client for
  Home Assistant (later phase), ArduinoOTA or HTTP OTA.
- **Fonts:** large numeric fonts should include only the glyphs needed
  (0–9, °, F, C, −, space, decimal point) to save flash and RAM.
- Use a partition table with two OTA app slots plus a filesystem (LittleFS) partition
  for web assets. 16MB leaves plenty of room.

---

## 5. Architecture

Single device. Suggested FreeRTOS structure:

1. **Sensor task** (highest priority): reads both MAX31856s every ~1s, applies light
   smoothing, computes rate of change (°/min over a rolling window), records faults.
2. **Alarm task** (high priority, same core as sensors or its own): evaluates
   thresholds and faults, drives the buzzer. Owns all safety decisions.
3. **UI task**: LVGL refresh and touch handling. Reads a shared, mutex-protected
   state struct; never calls sensor or network code directly.
4. **Network task**: Wi-Fi connection management, web server, MQTT, OTA. Must never
   block tasks 1–2.

Shared state: a single struct (temps, rates, fault flags, alarm state, settings,
timestamps) guarded by a mutex, or a queue-based snapshot. Settings changes from any
source (touchscreen, web page, MQTT) go through one validated setter that persists
to NVS.

Enable the task watchdog for the sensor and alarm tasks. Add Wi-Fi reconnect logic
with backoff; the old device's main complaint was flaky Wi-Fi needing manual resets.

---

## 6. Feature specification

### Measurements
- Stove temperature and flue temperature, °F by default, °C selectable.
- Rate of change for each, °F/min.
- Probe status per channel: OK / open / shorted / out of range.

### Alarms (all on-device)
- Per probe: **warning** and **critical** high thresholds.
- Optional low "fire is out / reload" threshold for the stove.
- **Flue rate-of-rise alarm:** a rapid flue climb is an early chimney-fire sign.
- **Probe fault alarm:** a failed probe must be visible and audible, never shown as
  a plausible temperature.
- Distinct beep patterns for warning vs critical vs fault.
- Silence/acknowledge from the touchscreen (and later web/HA); a silenced alarm
  re-arms if the condition worsens or after a timeout.
- Hysteresis on all thresholds to prevent chattering.

### "Ideal flue" indicator
Informational only, never an alarm. Lit when STOVE (collar) is within
`ideal_lo`..`ideal_hi` **and** FLUE (chimney exit) is at least `ideal_exit`, with
a 10°F deadband. Defaults 300–550°F and ≥250°F, chosen by Claude at Eddie's
request: in-pipe probe guidance is 275–500°F, catalytic stoves cruise ~400–550°F,
and exit gas below ~250°F condenses water and creosote. Revisit after real burns.

### Default thresholds (confirmed with Eddie, Phase 3)
| Setting | Default |
|---|---|
| STOVE warning / critical | 1000°F / 1100°F |
| FLUE warning / critical | 850°F / 950°F |
| FLUE rate-of-rise | 150°F/min, only starts when FLUE ≥ 500°F |
| Reload alert (STOVE low) | 300°F, arms after STOVE reaches reload + 2×hyst |
| Hysteresis | 25°F (rate alarm clears 20% below its limit) |
| Silence re-arm timeout | 10 min |
| Ideal burn indicator | STOVE 300–550°F and FLUE ≥ 250°F |

Unverified: a forum report says catalytic stoves can run ~1300°F at the collar for
~10 min during catalyst warm-up, which would trip the STOVE 1000/1100°F alarms on
every startup. Eddie chose these values; check them against the first real burn.

Defaults live in `settings::defaults()` (`src/settings.cpp`). Change values on a
running device with the serial console (`settings`, `set <key> <value>`,
`defaults`, `show`, `silence`, `buzzer`).

### Local display (UI v1) — built in Phase 4
Source of truth for the look is Eddie's Claude design canvas "Wood Stove Monitor"
(https://claude.ai/artifact/S58wVg4HUtADZJ1GLtnJEZ, 480×320 artboards). Code lives
in `src/ui/`: `dashboard` (artboards 1–3), `alarm_screens` (4–6), `settings_screen`
(7–8). Fonts (Plus Jakarta Sans 600/800, OFL) and icons are generated by
`tools/gen_assets.mjs` (`cd tools && npm install && npm run gen`); the generated
files in `src/ui/assets/` are committed and must stay **uncompressed** (LVGL has
compressed-font support off). Add any new glyph (e.g. `±`) to the generator.
Screen choice: an unsilenced Warning/Critical/chimney-fire alarm takes over the
whole display; faults, reload and silenced alarms stay on the dashboard.
Silence acknowledges only the conditions active at that moment; any new
condition re-arms. Quiet mode mutes Warning and Reload only.
Console `bench on` (RAM-only, clears on reboot) silences probe-fault beeps and
hides the fault strip for bench work with a terminal empty.

Original spec:
- Two large readouts (stove, flue) with status colour (normal / warning / critical /
  fault), rate-of-change arrows, ideal-flue icon, alarm banner.
- Small status row: Wi-Fi signal, MQTT state, last-update freshness.
- Settings screen: thresholds, units, brightness, alarm silence.

### Later (UI v2)
- History graphs (last few hours), reload button and elapsed time since reload,
  max temperature since reload, night dimming, optional SD-card logging.
- Burn-state estimate (COLD → STARTING → HEATING → CRUISING → COOLING → RELOAD) only
  after weeks of logged data exist to tune it.

### Web page (on the device)
- Live temperatures, rates, alarm state, and settings, reachable from any phone on
  the LAN. Works without Home Assistant.

### Outside temperature (added after Phase 5)
Display-only, never used by alarms. `src/weather.cpp` fetches current
temperature, sky and sunrise/sunset from Open-Meteo (free, no key) over HTTPS
every 15 min in its own priority-1 task on core 0; failures retry from 1 min,
doubling to 15 min, and the reading shows "--" once it is over an hour old.
TLS is verified against ISRG Root X1 (`src/weather_ca.h`, valid to 2035); if
Open-Meteo changes CA the web page shows the TLS error. The location is set on
the web page (paste Google Maps coordinates) or console `location <lat> <lon>`
/ `location clear` and is stored only in NVS namespace `weather` — **never put
Eddie's coordinates in the repo (it is public)**. The net task also syncs the
clock over NTP (UTC) once Wi-Fi connects. Top bar (right, beside the
gear): weather icon + "28°F" only (Eddie: no "outside" text), hidden if the
status chips need the room. Quiet mode is a crossed-out bell with no text;
moon/sun icons are reserved for weather and time.

### Home Assistant (later phase)
- Eddie does not run Home Assistant yet. Build MQTT support so it can be switched on
  later by setting a broker address.
- Use MQTT discovery (`homeassistant/...` config topics) for: both temperatures, both
  rates, alarm states, probe faults, Wi-Fi RSSI, and threshold number entities.
- Settings edited in HA flow through the same validated setter as the touchscreen.

---

## 7. Repository layout and secrets

```
stove-monitor/
├── CLAUDE.md
├── platformio.ini
├── partitions.csv            (two OTA slots + LittleFS)
├── include/
│   └── secrets.example.h     (template, committed)
├── src/                      (split into modules: sensors, alarms, ui, net, settings)
├── data/                     (web page assets for LittleFS)
└── .gitignore                (must include include/secrets.h and .pio/)
```

`include/secrets.h` holds Wi-Fi SSID/password and MQTT credentials. Never commit it.
Ask Eddie to fill it in; don't ask him to paste passwords into chat.

---

## 8. Known gotchas

- GPIO0 is both LCD RS and the BOOT strap pin. Fine in normal use; relevant only for
  recovery.
- LCD and touch share reset on GPIO4; leave the touch driver's reset pin unset (-1).
- If colours look wrong, check `invert` and `rgb_order` in the panel config. If touch
  points are mirrored or offset, check rotation and x/y min/max.
- PSRAM is quad, so GPIO35–37 are free for I2S (they would not be with octal PSRAM).
- USB hubs caused enumeration failures on Eddie's Mac — plug in directly.
- GPIO11 and GPIO14 are the native FSPID / FSPIWP pins; while the SPI peripheral
  owns them they idle HIGH, so pin-level probing with SPI attached is misleading.
- The board can sit near a hot stove; keep CPU load reasonable and don't run the
  backlight at 100% permanently.

---

## 9. Build phases (do in order; commit at the end of each)

**Phase 0 — Display bring-up (no extra hardware needed)**
Starter files already exist: `platformio.ini` and `src/main.cpp` (LovyanGFX config,
colour bars, touch drawing). Build and flash, then ask Eddie to confirm: text upright
in landscape, colour bars read red/green/blue/white, PSRAM ~2048KB and flash 16MB
shown, touch dots appear under his finger. Fix config until all pass.

**Phase 1 — Buzzer identification**
Determine active vs passive (see section 2). Write a small buzzer driver supporting
beep patterns.

**Phase 2 — Thermocouple bench test**
Requires Eddie to finish the junction box and pigtail wiring. Read both MAX31856s,
print temperatures and fault flags to serial and screen. Verify: room temperature is
plausible, warming a probe raises its reading, disconnecting a probe shows a fault.

**Phase 3 — Core firmware architecture**
Tasks, shared state, settings in NVS, alarm engine with hysteresis, watchdog.
Test alarms by temporarily lowering thresholds to near room temperature.

**Phase 4 — LVGL UI v1**
Main screen and settings screen per section 6.

**Phase 5 — Networking**
Wi-Fi with robust reconnect, local web page, OTA updates. After this phase, prefer
OTA over USB for routine updates.
**Eddie's request:** Wi-Fi is set up from the touchscreen — a "Wi-Fi" tab in
Settings that scans networks, lets him pick one and type the password on an
on-screen keyboard. Credentials are stored in NVS on the device (this replaces
`secrets.h` for Wi-Fi; never log or print the password). Add the Wi-Fi status
icon to the dashboard top bar.

**Phase 6 — Home Assistant MQTT discovery**
Only when Eddie has HA and the Mosquitto broker running.

**Phase 7 — UI v2 and history**
Graphs, reload tracking, night dimming, optional SD logging.

**Phase 8 — Burn-state logic**
After sufficient logged data.

---

## 10. Open questions for Eddie

Ask these when the relevant phase begins, not all at once:
- °F or °C default, preferred screen brightness, night dimming schedule. (Phase 4)
- Whether he wants SD-card history logging. (Phase 7)