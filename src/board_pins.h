/*
 * board_pins.h - Per-board hardware pin map.
 *
 * The unified firmware historically hardcoded the Seeed XIAO ESP32-S3 pinout
 * (buzzer GPIO3, user LED GPIO21, GPS 43/44). Adding the Adafruit ESP32-S3
 * Feather TFT as a build target requires those assignments to move, because
 * that board reserves several of the XIAO pins for the on-board ST7789 TFT,
 * the I2C/TFT power rail, and its NeoPixel.
 *
 * This header centralises the shared pin constants so mode code does not need
 * to know which board it is on. Each mode still defines its own BUZZER_PIN /
 * LED_PIN macros locally today; those should be migrated to include this
 * header, but to keep the change low-risk the constants below are namespaced
 * (OUISPY_*) and can be adopted incrementally.
 *
 * Board selection is driven by the PlatformIO env build flags:
 *   -DBOARD_XIAO_S3      (default, existing behaviour)
 *   -DBOARD_FEATHER_TFT  (new Adafruit ESP32-S3 Feather TFT target)
 *
 * When neither is defined we fall back to the XIAO map so existing builds are
 * unaffected.
 */
#ifndef OUISPY_BOARD_PINS_H
#define OUISPY_BOARD_PINS_H

#include <stdint.h>
#include <Arduino.h>   // HIGH / LOW

#if defined(BOARD_FEATHER_TFT)

// ---------------------------------------------------------------------------
// Adafruit ESP32-S3 Feather TFT (product 5483)
//
// The Arduino core for this board defines TFT_CS/TFT_DC/TFT_RST/TFT_BACKLITE/
// TFT_I2C_POWER and PIN_NEOPIXEL/NEOPIXEL_POWER in its pins_arduino.h, so the
// display driver references those macros directly rather than re-declaring
// them here. This header only owns the peripherals the modes drive.
//
// The Feather has no on-board buzzer. GPIO A0 (pin 18 / "A0" alias) is a free,
// PWM-capable pin on the STEMMA/analog header and is a safe default for an
// externally attached piezo. Set OUISPY_BUZZER_PIN to -1 to disable audio
// entirely and run display-only.
// ---------------------------------------------------------------------------
static const int OUISPY_BUZZER_PIN  = 18;   // A0 header pin; external piezo (optional)
static const int OUISPY_LED_PIN     = 13;   // on-board red LED (D13), non-inverted
static const bool OUISPY_LED_INVERTED = false;

// Feather GPS (Flock-You / Sky Spy). RX/TX broken out on the header; avoid the
// TFT and I2C-power pins. A1/A2 (pins 17/16) are free UART-capable pins.
static const int OUISPY_GPS_TX_PIN  = 17;   // ESP32 TX -> GPS RX
static const int OUISPY_GPS_RX_PIN  = 16;   // ESP32 RX <- GPS TX

#define OUISPY_HAS_TFT 1

#else  // BOARD_XIAO_S3 (default)

// ---------------------------------------------------------------------------
// Seeed Studio XIAO ESP32-S3 (original target). Matches the values already
// hardcoded across the mode sources; kept here for reference and future
// migration. LED uses inverted logic (HIGH = OFF).
// ---------------------------------------------------------------------------
static const int OUISPY_BUZZER_PIN  = 3;
static const int OUISPY_LED_PIN     = 21;
static const bool OUISPY_LED_INVERTED = true;

static const int OUISPY_GPS_TX_PIN  = 43;
static const int OUISPY_GPS_RX_PIN  = 44;

#define OUISPY_HAS_TFT 0

#endif

// ---------------------------------------------------------------------------
// Status-LED active level, board-aware.
//
// All mode code was written for the XIAO's on-board LED, which is active-LOW
// (LOW = lit, HIGH = dark). The Feather's on-board red LED (GPIO13) is
// active-HIGH, so the same literals leave it lit at idle and inverted during
// activity. These macros resolve to the correct level for the selected board
// via OUISPY_LED_INVERTED, so writing OUISPY_LED_ON / OUISPY_LED_OFF does the
// right thing everywhere. On the XIAO they expand to LOW / HIGH exactly as
// before, leaving that build byte-for-byte unchanged in behaviour.
// ---------------------------------------------------------------------------
#define OUISPY_LED_ON   (OUISPY_LED_INVERTED ? LOW  : HIGH)
#define OUISPY_LED_OFF  (OUISPY_LED_INVERTED ? HIGH : LOW)

#endif // OUISPY_BOARD_PINS_H
