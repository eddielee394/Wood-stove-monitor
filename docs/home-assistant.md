# Home Assistant

Home Assistant is the main way to see and control the stove monitor. The
monitor itself still makes every safety decision. Alarms and the buzzer work
the same with HA, the broker or Wi-Fi down.

## Setup (done once)

1. HA Green: install the **Mosquitto broker** add-on. In its Configuration,
   add a login under **Logins** (we use `stove-monitor`), save, and restart
   the add-on. Turn on **Start on boot**.
2. Settings → Devices & Services: the **MQTT** integration should show as
   configured. If it only shows as discovered, press **Configure**.
3. On the monitor's web page (http://192.168.1.98), open **Settings →
   Home Assistant (MQTT broker)**. Enter the HA Green's IP, port 1883, and the
   login, then save. Within a few seconds the line above the fields says
   "Connected", and HA shows a **Stove Monitor** device under MQTT.

## What HA gets

All entity IDs start with `stove_monitor_`.

| Kind | Entities |
|---|---|
| Sensors | Stove / Flue temperature, Stove / Flue rate (°F/min), Outside temperature, Burn zone (Too cool, Ideal burn, Running hot, Chimney too cool), Alarm level (none, reload, warning, fault, critical), Stove / Flue alarm (none, warning, critical), Silence time left |
| Binary sensors | Possible chimney fire, Stove / Flue probe fault, Reload needed, Ideal burn, Alarm silenced, Buzzer sounding |
| Switches | Reload alert, Quiet mode, Ideal burn indicator, Screen shows °C, Flip screen |
| Numbers | Stove / Flue warning and critical, Chimney fire rise rate, Chimney fire check above, Reload temperature, Hysteresis, Ideal stove from / to, Ideal chimney exit at least, Silence warnings for, Silence critical for, Screen brightness |
| Button | Silence alarm |
| Diagnostics | Wi-Fi signal, Uptime, Last restart reason, Last rejected setting |

Every setting the monitor has is here. A change from HA goes through the same
checks as the touchscreen. If the monitor rejects a value (for example a
warning at or above its critical), the control snaps back to the real value
and **Last rejected setting** says why.

If the monitor goes offline, all of its entities become **unavailable** about
20 seconds later.

## Notifications

Paste each automation into Settings → Automations → Create → ⋮ → Edit in
YAML. Replace `notify.mobile_app_YOUR_PHONE` with your phone's notify action
(Developer tools → Actions, search "mobile_app").

The urgent alerts use Android's `alarm_stream` channel. It plays as an alarm
even when the phone is on Do Not Disturb or silent.

### Urgent: possible chimney fire

```yaml
alias: Stove - possible chimney fire
mode: single
triggers:
  - trigger: state
    entity_id: binary_sensor.stove_monitor_possible_chimney_fire
    to: "on"
actions:
  - action: notify.mobile_app_YOUR_PHONE
    data:
      title: "🔥 POSSIBLE CHIMNEY FIRE"
      message: >-
        Flue rising {{ states('sensor.stove_monitor_flue_rate') }}°F/min at
        {{ states('sensor.stove_monitor_flue_temperature') }}°F. Close the door
        and air control. Get everyone out. Call 911.
      data:
        ttl: 0
        priority: high
        channel: alarm_stream
        tag: stove-chimney
```

### Urgent: critical temperature

```yaml
alias: Stove - critical
mode: single
triggers:
  - trigger: state
    entity_id: sensor.stove_monitor_alarm_level
    to: critical
conditions:
  - condition: state
    entity_id: binary_sensor.stove_monitor_possible_chimney_fire
    state: "off"
actions:
  - action: notify.mobile_app_YOUR_PHONE
    data:
      title: "Stove CRITICAL"
      message: >-
        Stove {{ states('sensor.stove_monitor_stove_temperature') }}°F,
        flue {{ states('sensor.stove_monitor_flue_temperature') }}°F.
      data:
        ttl: 0
        priority: high
        channel: alarm_stream
        tag: stove-critical
        actions:
          - action: STOVE_SILENCE
            title: Silence
```

### Urgent: probe fault

```yaml
alias: Stove - probe fault
mode: single
triggers:
  - trigger: state
    entity_id:
      - binary_sensor.stove_monitor_stove_probe_fault
      - binary_sensor.stove_monitor_flue_probe_fault
    to: "on"
    for: "00:00:30"
actions:
  - action: notify.mobile_app_YOUR_PHONE
    data:
      title: "Stove monitor: probe fault"
      message: "{{ trigger.to_state.name }}: no reading. Check the probe and the sensor box."
      data:
        ttl: 0
        priority: high
        channel: alarm_stream
        tag: stove-fault
```

### Urgent: monitor offline

```yaml
alias: Stove - monitor offline
mode: single
triggers:
  - trigger: state
    entity_id: sensor.stove_monitor_alarm_level
    to: unavailable
    for: "00:02:00"
actions:
  - action: notify.mobile_app_YOUR_PHONE
    data:
      title: "Stove monitor offline"
      message: >-
        Home Assistant can't reach the stove monitor. Its own alarms still
        work, but you won't get alerts on your phone.
      data:
        ttl: 0
        priority: high
        channel: alarm_stream
        tag: stove-offline
```

### Normal: warning

```yaml
alias: Stove - warning
mode: single
triggers:
  - trigger: state
    entity_id: sensor.stove_monitor_alarm_level
    to: warning
actions:
  - action: notify.mobile_app_YOUR_PHONE
    data:
      title: "Stove warning"
      message: >-
        Stove {{ states('sensor.stove_monitor_stove_temperature') }}°F,
        flue {{ states('sensor.stove_monitor_flue_temperature') }}°F.
      data:
        tag: stove-warning
        actions:
          - action: STOVE_SILENCE
            title: Silence
```

### Normal: time to reload

```yaml
alias: Stove - reload
mode: single
triggers:
  - trigger: state
    entity_id: binary_sensor.stove_monitor_reload_needed
    to: "on"
actions:
  - action: notify.mobile_app_YOUR_PHONE
    data:
      title: "Time to reload the stove"
      message: "Stove has cooled to {{ states('sensor.stove_monitor_stove_temperature') }}°F."
      data:
        tag: stove-reload
```

### Silence button in notifications

The **Silence** button on the critical and warning notifications presses the
monitor's Silence, the same as on the screen. Silence covers only the alarms
active at that moment; anything new re-arms.

```yaml
alias: Stove - silence from notification
mode: single
triggers:
  - trigger: event
    event_type: mobile_app_notification_action
    event_data:
      action: STOVE_SILENCE
actions:
  - action: button.press
    target:
      entity_id: button.stove_monitor_silence_alarm
```

## Dashboard

The full dashboard is `docs/ha-dashboard.yaml`. It has three views: Stove
(alarm banner, gauges, 6-hour graph, quick controls), Settings (every
setting) and History (24 h, 7 days, alarm log, monitor health). To install
or update it:

1. Settings → Dashboards → Add dashboard → New dashboard from scratch,
   named "Stove", icon `mdi:fireplace` (first time only).
2. Open it, then pencil (Edit) → ⋮ → Raw configuration editor.
3. Replace everything with the contents of `docs/ha-dashboard.yaml` and save.

## Adding a setting later

Every new monitor setting must also appear here (see CLAUDE.md, "Home
Assistant"). Add it to `SETTINGS` in `src/mqtt.cpp` and to
`docs/ha-dashboard.yaml`. Don't add it to the device web page.
