/*
 * tft_display.h - Adafruit ESP32-S3 Feather TFT graphical detection UI.
 *
 * Mirrors the existing display_dongle.h stub pattern: a thin, always-callable
 * interface that compiles to no-ops unless OUISPY_HAS_TFT is set (which the
 * Feather build flag -DBOARD_FEATHER_TFT enables via board_pins.h). This lets
 * main.cpp call the display hooks unconditionally without #ifdef clutter at
 * the call sites.
 *
 * The driver is a pure consumer of DetectionFeed: it never touches radios or
 * mode state. Once per tick it pulls a snapshot and repaints only when the
 * feed revision changed (dirty check) so the loop stays responsive.
 */
#ifndef OUISPY_TFT_DISPLAY_H
#define OUISPY_TFT_DISPLAY_H

#include <stdint.h>
#include "board_pins.h"

#if OUISPY_HAS_TFT

// Bring up the ST7789, backlight and TFT power rail, and paint a splash.
void tftUiInit();

// Announce a new active mode (mode index 0 == boot selector). Repaints the
// frame chrome. Safe to call every boot; cheap if unchanged.
void tftUiSetMode(int modeIndex, const char* name, const char* desc);

// Call every main-loop iteration. Cheap; repaints only on feed changes or the
// periodic clock/animation refresh.
void tftUiTick(uint32_t nowMs);

// ---- On-device selector navigation (single BOOT button) -----------------
// When the boot selector (mode 0) is active, the display shows a highlighted
// mode menu instead of the idle detection screen. main.cpp drives it:
//   tftUiMenuCount()          number of selectable modes (for wrap-around)
//   tftUiMenuHighlight(i)     move the highlight to entry i (0-based)
//   tftUiMenuSelected()       read the current highlight
int  tftUiMenuCount();
void tftUiMenuHighlight(int index);
int  tftUiMenuSelected();

#else  // ---- no display: inline no-ops ----

static inline void tftUiInit() {}
static inline void tftUiSetMode(int, const char*, const char*) {}
static inline void tftUiTick(uint32_t) {}
static inline int  tftUiMenuCount() { return 0; }
static inline void tftUiMenuHighlight(int) {}
static inline int  tftUiMenuSelected() { return 0; }

#endif // OUISPY_HAS_TFT

#endif // OUISPY_TFT_DISPLAY_H
