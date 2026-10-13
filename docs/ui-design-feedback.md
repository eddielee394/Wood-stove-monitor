# Wood Stove Monitor artboards: firmware feedback

Please update the "Wood Stove Monitor" canvas to match how the firmware works.
The screen is 480×320 and the touch targets stay as they are.

## Probe facts the design should reflect
- **STOVE** probe: on the flue pipe just above the stove collar. This is the
  standard place to judge a burn.
- **FLUE** probe: at the top of the chimney, where the gas exits.
- Catalytic stove, double-wall pipe. All temperatures are °F by default.

## 1. Buzzer mute (Settings + status bar)
A full "alarms show on screen only" mute isn't allowed. Safety alarms must
always sound. Change the toggle to **"Quiet mode"**, which mutes only
**Warning** and **Reload** alerts. **Critical, Chimney fire and Probe fault
always sound.** Hint text: "Critical alarms always sound". The status-bar
"Muted" label becomes "Quiet".

## 2. Burn zone bar (Main, Probe fault, History)
Drive it from the **STOVE (collar) probe**, not FLUE:
- COOL below 300°F · IDEAL 300–550°F · HOT above 550°F
- "Ideal burn" also requires FLUE (chimney exit) to be at least 250°F. If
  STOVE is in range but FLUE is below 250°F, show "Chimney too cool ·
  creosote risk".
- Rename the section label "BURN ZONE" (it isn't flue-only now).
- History: shade the ideal band on the STOVE line (300–550°F) and label it
  "IDEAL".

## 3. Silence durations
- **Critical and Chimney fire:** "Silence 2 min". The alarm returns early if
  the reading keeps getting worse.
- **Warning and Probe fault:** "Silence 10 min".
- Keep the existing note style ("Alarm returns while the flue keeps climbing").

## 4. Alarm thresholds (Settings + alarm screens)
Each probe has a **warning** and a **critical** limit:

| | Warning | Critical |
|---|---|---|
| STOVE | 1000°F | 1100°F |
| FLUE | 850°F | 950°F |

- Settings needs four limit rows (25°F steps). The screen may scroll, or use
  a "Stove" / "Flue" tab pair, whichever reads better at 480×320.
- Settings also needs: Reload alert (on/off, 300°F), Quiet mode, Brightness.
- The **High temperature** screen (orange) is the **warning** level. Use real
  values, e.g. FLUE 870°F, "Warning 850°F". Add a matching **critical** variant
  (red, rapid-beep, "Silence 2 min"), e.g. STOVE 1120°F, "Critical 1100°F".
- The **Chimney fire** screen fires on a FLUE rise of ≥150°F/min while FLUE is
  above 500°F. Example values: FLUE 720°F, +180°F/min.

## 5. Add a Reload screen state
A gentle, non-urgent state when the stove cools below 300°F after being hot:
a blue banner "Time to reload", with an "OK" button that dismisses it. Keep
the dashboard visible beneath it.

## 6. Out of scope for the first build (keep the artboards, mark them "later")
- The **clock** in the status bar needs network time (Phase 5). Until then
  the status bar shows Wi-Fi, quiet mode and sensor freshness.
- **History** comes in Phase 7.

## 7. Font
Plus Jakarta Sans is fine. Use only weights 600 and 800, and keep 96px to
digits and "°F", so the firmware can convert just those glyphs.
