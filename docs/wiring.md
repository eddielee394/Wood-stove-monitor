# Wiring: display to sensor box

As built by Eddie (2026-10-16) and verified working: both probes read with
no faults over the Cat6 link.

The display's 8-pin I/O header (MX1.25 pigtail) connects to an RJ45 jack on
the display unit, then a Cat6 patch cable goes to an RJ45 jack in the sensor
box, which is wired to the two MAX31856 breakouts. Both jacks are punched
down **T568B**, and the cable must be a straight-through patch cable.

| Pigtail wire (header pin) | Signal | Cat6 colour | RJ45 pin (T568B) | Lands on |
|---|---|---|---|---|
| Red (1) | +5V | Brown | 8 | VIN, both breakouts |
| Black (2) | GND | Orange/white + Brown/white | 1 and 7 | GND, both breakouts |
| Yellow (3) | CS stove (GPIO10) | Green/white | 3 | CS, **right** breakout (STOVE) |
| Green (4) | MOSI (GPIO11) | Green | 6 | SDI, both breakouts |
| Blue (5) | SCK (GPIO12) | Blue | 4 | SCK, both breakouts |
| White (6) | MISO (GPIO13) | Blue/white | 5 | SDO, both breakouts |
| Orange (7) | CS flue (GPIO14) | Orange | 2 | CS, **left** breakout (FLUE) |
| Brown (8) | Buzzer (GPIO21) | none | none | Stays inside the display |

Notes:
- The **right** breakout is STOVE and the **left** is FLUE, as seen when
  working in the sensor box.
- GND uses two conductors (pins 1 and 7). Each one shares a twisted pair
  with a supply or select line: brown/white with +5V, and orange/white with
  CS flue.
- The pigtail's brown wire (buzzer) and the Cat6 brown conductor (+5V) are
  different signals. Don't match them up by colour.
- **Never plug either jack into a router, switch or wall Ethernet port.** PoE
  switches put up to ~57V on the cable and would destroy both boards.

Troubleshooting: "No sensor" on **both** probes means one of the shared lines
(+5V, GND, SCK, MOSI, MISO) is not getting through. "No sensor" on **one**
probe points to that probe's CS wire (or its breakout). "No signal" means the
breakout is answering but its thermocouple is open, which is a probe or
probe-plug problem rather than this cable.
