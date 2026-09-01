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

## On-device navigation (single BOOT button)

The Feather's only usable user button is BOOT (GPIO0); the second physical
button is RESET and cannot be read. Navigation is therefore driven from BOOT
alone:

- **Tap** (release before 1.5 s): in the boot selector, advance the on-screen
  mode highlight. In a running mode, no-op.
- **Hold** (>= 1.5 s): in the boot selector, launch the highlighted mode; in a
  running mode, return to the selector. Confirmed by three chirps.

The selector renders a highlighted mode menu (`SELECT MODE`) on the TFT so the
web UI is no longer required to change modes. On the display-less XIAO,
`tftUiMenuCount()` is 0 and the hold falls back to the original
return-to-selector behaviour, unchanged.

## Colour theme

The palette mirrors the oui-spy-unified-blue web theme
(colonelpanichacks.github.io/oui-spy-unified-blue), converted to RGB565:
near-black teal background (`#030805`), neon green primary accent (`#00ff66`),
cyan (`#00e5ff`), magenta for alerts/errors (`#ff2bd6`), amber for warnings
(`#ffb000`), body text `#cfe8d8`, muted labels `#6a8878`. Detection kind chips:
BLE cyan, WiFi green, UAS (drone) magenta, GPS lime, META (Meta/Ray-Ban
glasses) magenta. The Detector classifies composite Meta/Ray-Ban hits
(`meta_composite`) as `DetKind::Meta` so they render with a distinct magenta
`META` chip, mirroring the web dashboard's dedicated META badge.

### Flock-You confidence filtering

Flock-You's OUI match tiers 0-2 (SSID keyword, receiver/BSSID OUI echo, and
bare transmitter OUI) collide with ordinary Wi-Fi silicon vendors, so an office
full of phones/laptops/IoT floods the screen with non-camera hits. The TFT
therefore surfaces only tier >= 3: tier 3 (`wildcard_probe`) and tier 4
(`wildcard_probe_ie_sig`) are Flock-shaped. These push as `DetKind::Flock` with
their tier, rendering a `FLK3` (amber) or `FLK4` (green) chip; tier 4 also draws
its label white for emphasis. Tiers 0-2 still beep and log over serial/web as
before, they are just kept off the graphical detection list. The on-screen
unique count is therefore high-confidence cameras only.

To help identify what a tier-3 hit actually is, Flock rows label themselves with
the transmitter **MAC** (so the OUI is visible at a glance) rather than the
match method. When the OUI is a known common consumer Wi-Fi chipset vendor —
i.e. a probable false positive, since tier 3 is just "OUI match + active
scanning" — the second line shows an amber `FP? <vendor>` annotation in place of
the RSSI bar (the numeric RSSI is kept). The vendor set lives in
`kCommonSiliconOuis` in `raw/flockyou_promiscious.cpp`; it is a small,
high-confidence starter list (seeded with Espressif) meant to be extended from
the IEEE OUI registry. An OUI a Flock camera actually uses must never be added
there, or real cameras would be flagged as false positives.

## Status LED

Mode code was written for the XIAO's active-LOW LED. The Feather's on-board red
LED (GPIO13) is active-HIGH, so the raw literals left it lit at idle. Board-
aware `OUISPY_LED_ON`/`OUISPY_LED_OFF` (and per-file `LED_ON_LVL`/`LED_OFF_LVL`,
and flockyou's `LED_ACTIVE_HIGH`) now resolve to the correct level via
`OUISPY_LED_INVERTED`. The XIAO build expands to the original `LOW`/`HIGH` and
is behaviourally unchanged.

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

## Mode integration status

All six modes publish to the feed. Each mode's wrapper `mode_*.cpp` includes
`detection_feed.h` (outside the anonymous namespace) and pushes at its existing
detection/alert choke point:

| Mode | Hook site | Kind | Notes |
|------|-----------|------|-------|
| Detector (`raw/detector.cpp`) | `bleNoteDetection()` | `BLE` | `isNew` from table upsert |
| Foxhunter (`raw/foxhunter.cpp`) | tracking loop | — | drives the proximity gauge via `setProximity()` |
| Flock-You WiFi (`raw/flockyou_promiscious.cpp`) | emit path beside `dongleDisplayShowAlert()` | `Flock` | only tier >= 3 (wildcard probe / probe+IE sig) reaches the TFT; tiers 0-2 (SSID/OUI-echo/bare-OUI) are suppressed as vendor-OUI noise; chip shows `FLK4`/`FLK3` coloured by confidence |
| PCAP (`raw/pcap.cpp`) | `promisc_cb()` after `ring_push_bytes` | `WiFi` | firehose: throttled to ~6/s, 16-entry transmitter-MAC cache for `isNew`; labelled mgmt/ctrl/data |
| Sky Spy (`raw/skyspy.cpp`) | `printerTask()` queue consumer | `Drone` | single choke point for all BLE + WiFi Remote ID paths; runs in task (not ISR) context |
| BLE Sniff (`raw/blesniff.cpp`) | `Cb::onResult()` after `ring_push` | `BLE` | firehose: throttled to ~6/s, 16-entry MAC cache for `isNew` |

### Firehose throttling

PCAP and BLE Sniff capture at very high rates. Their hooks throttle surfaced
events to roughly six per second and keep a small recently-seen-MAC ring so a
brand-new device is always surfaced immediately (and counted as unique) while
repeat traffic is rate-limited. This keeps the SPI blit off the capture hot
path and the counters meaningful without a full device table.

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
