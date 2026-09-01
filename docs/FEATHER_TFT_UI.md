# Adafruit ESP32-S3 Feather TFT — Graphical Detection UI

This document describes the design and implementation of the graphical
detection UI added to the OUI Spy unified firmware, targeting the Adafruit
ESP32-S3 Feather TFT (product 5483) with its on-board 240x135 ST7789 display.
The goal is to show detections visually on-screen instead of relying on the
buzzer.

## Design goals

1. Zero impact on the existing Seeed XIAO ESP32-S3 build. The XIAO has no
   display; all display code compiles to no-ops there.
2. Decouple modes from the display. Modes should not know a screen exists;
   the display should not know mode internals.
3. Preserve the existing buzzer/LED behaviour. The TFT is an additional,
   primary indicator, not a replacement that removes audio for XIAO users.
4. Keep the render off the hot path — no blocking, no per-frame full redraws
   unless state actually changed.

## Architecture

```
  modes (detector, foxhunter, ...)          main.cpp loop()
          |                                        |
          | DetectionFeed::pushDetection()         | tftUiTick(millis())
          v                                        v
   +---------------------+   getSnapshot()   +------------------+
   |   DetectionFeed     | <---------------- |  tft_display.cpp |
   | (global event bus)  |                   | (ST7789 renderer)|
   +---------------------+                   +------------------+
```

### Components added

| File | Role |
|------|------|
| `src/board_pins.h` | Per-board pin map, selected by `-DBOARD_XIAO_S3` / `-DBOARD_FEATHER_TFT`. Defines `OUISPY_HAS_TFT`. |
| `src/detection_feed.h/.cpp` | Mode-agnostic detection event bus: ring of recent events, running counters, decaying rate, and an RSSI-proximity channel. Always compiled; POD; guarded by a `portMUX` critical section. |
| `src/tft_display.h/.cpp` | ST7789 renderer. Consumes a `DetectionFeed::Snapshot` each tick. Compiles to inline no-ops when `OUISPY_HAS_TFT == 0`. |

### Wiring into the firmware

- `main.cpp`
  - `tftUiInit()` in `setup()` (splash while WiFi resets).
  - `DetectionFeed::beginMode()` + `tftUiSetMode()` after mode routing, so the
    header is populated for every mode.
  - `tftUiTick(millis())` at the top of `loop()`.
- `mode_detector.cpp` / `raw/detector.cpp`
  - `DetectionFeed::pushDetection(DetKind::BLE, ...)` in the single BLE
    detection choke point `bleNoteDetection()`.
- `mode_foxhunter.cpp` / `raw/foxhunter.cpp`
  - `DetectionFeed::setProximity(rssi, targetMAC, detected)` in the tracking
    loop, driving the large proximity gauge.

## UI layout (240x135, landscape, rotation 3)

Rendered flicker-free via an off-screen `GFXcanvas16` in PSRAM, blitted with
`drawRGBBitmap()`.

- Header bar: mode name (left) + status chip and dot (right; green SCAN,
  blinking red HIT!, grey IDLE).
- Body (scanning modes): large UNIQUE-hit counter on the left, a most-recent-
  first list of detections on the right — each row shows a colour-coded kind
  chip (BLE/WIFI/UAS/GPS), the device label/MAC, and an RSSI strength bar.
- Body (RSSI/proximity modes): a large horizontal strength gauge, RSSI in dBm,
  and a qualitative distance band (FAR → NEAR → CLOSE → ON TOP), or a blinking
  SEARCHING state when the target is out of range.
- Footer: hits/min, seen (unique/total), and uptime.

## Building

```bash
pio run -e feather_tft                 # build for the Feather TFT
pio run -e feather_tft -t upload       # flash
pio run -e esp32s3                     # unchanged XIAO build
```

## Board pin remap (Feather TFT)

GPIO21 on the XIAO is the user LED, but on the Feather it is the TFT/I2C power
rail. Every mode previously hardcoded `LED_PIN 21` and `BUZZER_PIN 3`; those
defines are now guarded with `#ifdef BOARD_FEATHER_TFT` so the Feather build
uses GPIO13 (on-board red LED) and GPIO18/A0 (optional external piezo) and
never toggles the display power rail. The ST7789 control pins
(`TFT_CS/TFT_DC/TFT_RST/TFT_BACKLITE/TFT_I2C_POWER`) come from the Adafruit
core's `pins_arduino.h`.

## Extending to the remaining modes

The detector (BLE) and foxhunter (RSSI) are wired as reference integrations.
To surface detections from the other modes, add a single `pushDetection()`
call at each mode's existing detection/alert site:

| Mode | Suggested hook | Kind |
|------|----------------|------|
| Flock-You WiFi (`raw/flockyou_promiscious.cpp`) | where a matched OUI/probe signature is logged | `DetKind::WiFi` |
| PCAP (`raw/pcap.cpp`) | per captured frame of interest (throttle to avoid flooding) | `DetKind::WiFi` |
| Sky Spy (`raw/skyspy.cpp`) | in the drone-detection beep trigger | `DetKind::Drone` |
| BLE Sniff (`raw/blesniff.cpp`) | per advertising packet added to the capture | `DetKind::BLE` |

Each requires: add `#include "detection_feed.h"` to the mode's wrapper
`mode_*.cpp` (outside the anonymous namespace) and one `pushDetection()` line
at the detection site.

## Known limitations / follow-ups

- GPS pins for Flock-You / Sky Spy are still the XIAO values (43/44). If GPS is
  used on the Feather, migrate those to `OUISPY_GPS_TX_PIN`/`OUISPY_GPS_RX_PIN`
  from `board_pins.h`.
- Not yet hardware-validated; the firmware could not be compiled or flashed in
  the development environment (no PlatformIO toolchain or Feather present). The
  detection-feed logic was unit-checked on the host; the ST7789 rendering path
  needs a bench test on the target.
- The buzzer remains active on the Feather when a piezo is attached to A0;
  set `OUISPY_BUZZER_PIN` to -1 in `board_pins.h` for a silent, display-only
  build.
