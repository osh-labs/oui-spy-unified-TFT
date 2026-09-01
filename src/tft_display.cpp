/*
 * tft_display.cpp - graphical detection UI for the Adafruit ESP32-S3 Feather TFT.
 *
 * Only compiled when OUISPY_HAS_TFT is defined (Feather build). Renders the
 * DetectionFeed snapshot to the 240x135 ST7789.
 *
 * Rendering strategy
 * ------------------
 * We draw into an off-screen GFXcanvas16 in PSRAM and blit the whole 240x135
 * frame with drawRGBBitmap(). Full-frame double-buffering is flicker-free and
 * keeps the drawing code straightforward (no dirty-rectangle bookkeeping).
 * A 240x135x2 = ~63 KB canvas is trivial for the Feather's 2 MB PSRAM.
 *
 * We only repaint when the feed revision changes or once a second for the
 * clock/animation, so the blit cost (~a few ms over SPI) stays off the hot path.
 *
 * Layout (landscape, rotation 3):
 *   +--------------------------------------------------+  0
 *   | MODE NAME                              [status]  |  header bar (18px)
 *   +--------------------------------------------------+  18
 *   | UNIQUE            recent detections list         |
 *   |  count            (label / mac / rssi bar)       |  body
 *   +--------------------------------------------------+  122
 *   | rate    seen    uptime                           |  footer (13px)
 *   +--------------------------------------------------+  135
 *
 * Foxhunter and other RSSI modes replace the body with a large proximity
 * gauge when DetectionFeed reports an active proximity channel.
 */
#include "tft_display.h"

#if OUISPY_HAS_TFT

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#include "detection_feed.h"

namespace {

// 240x135 in rotation 3 (USB port on the left, standard Feather orientation).
constexpr int16_t W = 240;
constexpr int16_t H = 135;

// Colour palette (RGB565), matched to the oui-spy-unified-blue web theme
// (colonelpanichacks.github.io/oui-spy-unified-blue). Hex -> RGB565 alongside.
constexpr uint16_t C_BG      = 0x0040;  // #030805 near-black teal (page bg)
constexpr uint16_t C_PANEL   = 0x0920;  // #0a2015 grid/panel fill
constexpr uint16_t C_FRAME   = 0x07EC;  // #00ff66 neon green (primary accent)
constexpr uint16_t C_DIM     = 0x09C4;  // #0a3a20 green-dim (dividers/fills)
constexpr uint16_t C_TEXT    = 0xCF5B;  // #cfe8d8 body text
constexpr uint16_t C_WHITE   = 0xFFFF;  // #ffffff emphasis
constexpr uint16_t C_GREY    = 0x6C4F;  // #6a8878 muted labels
constexpr uint16_t C_ALERT   = 0xF95A;  // #ff2bd6 magenta (alerts/errors)
constexpr uint16_t C_AMBER   = 0xFD80;  // #ffb000 amber (warnings)
constexpr uint16_t C_BLE     = 0x073F;  // #00e5ff cyan
constexpr uint16_t C_WIFI    = 0x07EC;  // #00ff66 green
constexpr uint16_t C_DRONE   = 0xF95A;  // #ff2bd6 magenta
constexpr uint16_t C_GPS     = 0xAFE7;  // #a8ff3e lime
constexpr uint16_t C_CYAN    = 0x073F;  // #00e5ff cyan (secondary accent / panels)

Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16*     cv  = nullptr;

int      gModeIndex = -1;
char     gModeName[20] = "OUI SPY";
uint32_t gLastRev = 0xFFFFFFFF;
uint32_t gLastPaint = 0;
uint32_t gLastBlink = 0;
bool     gBlink = false;

// On-device selector menu state. Active only while mode 0 (boot selector) is
// the current mode. Entry i corresponds to firmware mode (i + 1).
const char* const kMenuNames[] = {
    "DETECTOR", "FOXHUNTER", "FLOCK-YOU", "PCAP", "SKY SPY", "BLE SNIFF"
};
const char* const kMenuDescs[] = {
    "BLE/WiFi surveillance", "RSSI proximity tracker", "2.4GHz sniffer",
    "WiFi packet capture", "drone Remote ID", "BLE advertising capture"
};
constexpr int kMenuCount = sizeof(kMenuNames) / sizeof(kMenuNames[0]);
bool gInSelector = false;
int  gMenuSel    = 0;
uint32_t gMenuRev = 0;   // bumped on highlight change to force a repaint

uint16_t kindColor(DetectionFeed::DetKind k) {
    using K = DetectionFeed::DetKind;
    switch (k) {
        case K::BLE:   return C_BLE;
        case K::WiFi:  return C_WIFI;
        case K::Drone: return C_DRONE;
        case K::GPS:   return C_GPS;
        case K::Meta:  return C_ALERT;   // magenta, matching the web META badge
        default:       return C_GREY;
    }
}

const char* kindTag(DetectionFeed::DetKind k) {
    using K = DetectionFeed::DetKind;
    switch (k) {
        case K::BLE:   return "BLE";
        case K::WiFi:  return "WIFI";
        case K::Drone: return "UAS";
        case K::GPS:   return "GPS";
        case K::Meta:  return "META";
        default:       return "?";
    }
}

// Map an RSSI (dBm) to a 0..100 "strength" for bar fills.
int rssiPct(int8_t rssi) {
    if (rssi == 0) return 0;
    int v = rssi;                    // typically -30 (strong) .. -95 (weak)
    if (v > -35) v = -35;
    if (v < -95) v = -95;
    return (int)((v + 95) * 100 / 60);   // -95 -> 0, -35 -> 100
}

uint16_t rssiColor(int8_t rssi) {
    int p = rssiPct(rssi);
    if (p >= 66) return C_FRAME;
    if (p >= 33) return C_AMBER;
    return C_ALERT;
}

void drawHeader(const DetectionFeed::Snapshot& s) {
    cv->fillRect(0, 0, W, 18, C_BG);
    cv->drawFastHLine(0, 18, W, C_FRAME);

    cv->setTextSize(2);
    cv->setTextColor(C_WHITE);
    cv->setCursor(3, 2);
    // modeName from feed if present, else the cached name.
    const char* name = s.modeName[0] ? s.modeName : gModeName;
    cv->print(name);

    // Status indicator top-right.
    using St = DetectionFeed::DetStatus;
    uint16_t dot = C_GREY;
    const char* txt = "IDLE";
    switch (s.status) {
        case St::Scanning: dot = C_FRAME; txt = "SCAN"; break;
        case St::Alert:    dot = gBlink ? C_ALERT : C_AMBER; txt = "HIT!"; break;
        case St::Error:    dot = C_ALERT; txt = "ERR"; break;
        default:           break;
    }
    cv->setTextSize(1);
    cv->setTextColor(dot);
    int16_t tw = (int16_t)strlen(txt) * 6;
    cv->setCursor(W - tw - 14, 5);
    cv->print(txt);
    cv->fillCircle(W - 7, 8, 4, dot);
}

void drawFooter(const DetectionFeed::Snapshot& s, uint32_t now) {
    int16_t y = H - 13;
    cv->drawFastHLine(0, y, W, C_FRAME);
    cv->setTextSize(1);
    cv->setTextColor(C_GREY);

    char buf[48];
    // rate
    snprintf(buf, sizeof(buf), "%.0f/min", s.hitsPerMin);
    cv->setCursor(3, y + 3);
    cv->print(buf);

    // seen counters
    snprintf(buf, sizeof(buf), "seen:%lu/%lu",
             (unsigned long)s.uniqueHits, (unsigned long)s.totalHits);
    int16_t tw = (int16_t)strlen(buf) * 6;
    cv->setCursor((W - tw) / 2, y + 3);
    cv->print(buf);

    // uptime mm:ss
    uint32_t up = now / 1000;
    snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(up / 60),
             (unsigned long)(up % 60));
    tw = (int16_t)strlen(buf) * 6;
    cv->setCursor(W - tw - 3, y + 3);
    cv->print(buf);
}

// The count + recent-list body used by scanning modes.
void drawDetectionBody(const DetectionFeed::Snapshot& s, uint32_t now) {
    const int16_t bodyTop = 21;

    // Left column: giant unique-hit count.
    cv->setTextColor(s.status == DetectionFeed::DetStatus::Alert && gBlink
                         ? C_ALERT : C_FRAME);
    cv->setTextSize(4);
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "%lu", (unsigned long)s.uniqueHits);
    cv->setCursor(6, bodyTop + 10);
    cv->print(cnt);
    cv->setTextSize(1);
    cv->setTextColor(C_GREY);
    cv->setCursor(6, bodyTop + 46);
    cv->print("UNIQUE");

    // Vertical divider.
    cv->drawFastVLine(72, bodyTop, H - bodyTop - 14, C_CYAN);

    // Right column: recent detections, most-recent first.
    const int16_t listX = 78;
    const int16_t rowH  = 16;
    int16_t y = bodyTop;
    if (s.recentCount == 0) {
        cv->setTextColor(C_GREY);
        cv->setTextSize(1);
        cv->setCursor(listX, bodyTop + 24);
        cv->print("waiting for");
        cv->setCursor(listX, bodyTop + 34);
        cv->print("detections...");
        return;
    }

    for (int i = 0; i < s.recentCount && y < H - 26; i++) {
        const DetectionFeed::DetEvent& e = s.recent[i];
        uint16_t kc = kindColor(e.kind);

        // Kind tag chip.
        cv->fillRect(listX, y, 26, 9, kc);
        cv->setTextColor(C_BG);
        cv->setTextSize(1);
        cv->setCursor(listX + 2, y + 1);
        cv->print(kindTag(e.kind));

        // Label (truncated by canvas clipping).
        cv->setTextColor(i == 0 ? C_WHITE : C_TEXT);
        cv->setCursor(listX + 30, y + 1);
        char lbl[18];
        strlcpy(lbl, e.label[0] ? e.label : (e.mac[0] ? e.mac : "unknown"),
                sizeof(lbl));
        cv->print(lbl);

        // RSSI mini-bar on the next line.
        int16_t barY = y + 10;
        int16_t barX = listX;
        int16_t barW = 120;
        cv->drawRect(barX, barY, barW, 4, C_CYAN);
        int pct = rssiPct(e.rssi);
        if (pct > 0) {
            cv->fillRect(barX + 1, barY + 1, (barW - 2) * pct / 100, 2,
                         rssiColor(e.rssi));
        }
        if (e.rssi != 0) {
            cv->setTextColor(C_GREY);
            cv->setCursor(barX + barW + 4, barY - 2);
            char rb[8];
            snprintf(rb, sizeof(rb), "%d", e.rssi);
            cv->print(rb);
        }
        y += rowH;
    }
}

// Large proximity gauge for Foxhunter-style RSSI tracking.
void drawProximityBody(const DetectionFeed::Snapshot& s) {
    const int16_t top = 24;
    cv->setTextSize(1);
    cv->setTextColor(C_GREY);
    cv->setCursor(6, top);
    cv->print("TARGET ");
    cv->setTextColor(C_WHITE);
    cv->print(s.proximityTarget[0] ? s.proximityTarget : "(none set)");

    if (!s.proximityLocked) {
        cv->setTextSize(3);
        cv->setTextColor(gBlink ? C_AMBER : C_GREY);
        cv->setCursor(40, 60);
        cv->print("SEARCHING");
        return;
    }

    int pct = rssiPct(s.proximityRssi);
    uint16_t col = rssiColor(s.proximityRssi);

    // Big horizontal strength bar.
    int16_t bx = 12, by = 42, bw = W - 24, bh = 34;
    cv->drawRect(bx, by, bw, bh, C_FRAME);
    cv->fillRect(bx + 2, by + 2, (bw - 4) * pct / 100, bh - 4, col);

    // RSSI value + qualitative distance band.
    cv->setTextSize(2);
    cv->setTextColor(C_WHITE);
    char rb[16];
    snprintf(rb, sizeof(rb), "%d dBm", s.proximityRssi);
    cv->setCursor(12, 86);
    cv->print(rb);

    const char* band = "FAR";
    if (pct >= 80) band = "ON TOP";
    else if (pct >= 60) band = "CLOSE";
    else if (pct >= 40) band = "NEAR";
    else if (pct >= 20) band = "MID";
    cv->setTextColor(col);
    int16_t tw = (int16_t)strlen(band) * 12;
    cv->setCursor(W - tw - 12, 86);
    cv->print(band);
}

// On-device mode picker shown while the boot selector (mode 0) is active.
void drawMenu() {
    cv->fillScreen(C_BG);

    // Header.
    cv->fillRect(0, 0, W, 18, C_BG);
    cv->drawFastHLine(0, 18, W, C_FRAME);
    cv->setTextSize(2);
    cv->setTextColor(C_WHITE);
    cv->setCursor(3, 2);
    cv->print("SELECT MODE");

    // Menu rows. Six entries in the 18..122 band -> ~17 px each.
    const int16_t top = 21, rowH = 17;
    for (int i = 0; i < kMenuCount; i++) {
        int16_t y = top + i * rowH;
        bool sel = (i == gMenuSel);
        if (sel) {
            cv->fillRect(2, y, W - 4, rowH - 2, C_FRAME);   // highlight bar
        }
        cv->setTextSize(1);
        cv->setTextColor(sel ? C_BG : C_FRAME);
        cv->setCursor(6, y + 1);
        cv->print(kMenuNames[i]);
        cv->setTextColor(sel ? C_BG : C_GREY);
        cv->setCursor(96, y + 1);
        cv->print(kMenuDescs[i]);
    }

    // Footer hint.
    int16_t fy = H - 13;
    cv->drawFastHLine(0, fy, W, C_FRAME);
    cv->setTextSize(1);
    cv->setTextColor(C_GREY);
    cv->setCursor(3, fy + 3);
    cv->print("tap: next   hold: launch");

    tft.drawRGBBitmap(0, 0, cv->getBuffer(), W, H);
}

void render(uint32_t now) {
    if (gInSelector) {
        drawMenu();
        return;
    }

    DetectionFeed::Snapshot s;
    DetectionFeed::getSnapshot(s);

    cv->fillScreen(C_BG);
    // Teal body panel with a cyan hairline border, echoing the web theme's
    // translucent panels so the scanning screen visibly carries the palette
    // rather than reading as plain green-on-black.
    cv->fillRect(0, 19, W, H - 19 - 12, C_PANEL);
    cv->drawRect(1, 20, W - 2, H - 20 - 13, C_CYAN);
    drawHeader(s);
    if (s.proximityActive) {
        drawProximityBody(s);
    } else {
        drawDetectionBody(s, now);
    }
    drawFooter(s, now);

    tft.drawRGBBitmap(0, 0, cv->getBuffer(), W, H);
}

} // namespace

void tftUiInit() {
    // Power the TFT + I2C rail (required on the Feather TFT).
#ifdef TFT_I2C_POWER
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
    delay(10);
#endif

    tft.init(135, 240);       // ST7789 240x135 panel
    tft.setRotation(3);
    tft.fillScreen(C_BG);

#ifdef TFT_BACKLITE
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);
#endif

    // Off-screen canvas in PSRAM (falls back to internal RAM if PSRAM is off,
    // where it is still small enough to allocate).
    cv = new GFXcanvas16(W, H);

    // Splash.
    cv->fillScreen(C_BG);
    cv->drawRect(0, 0, W, H, C_FRAME);
    cv->setTextSize(4);
    cv->setTextColor(C_FRAME);
    cv->setCursor(24, 40);
    cv->print("OUI SPY");
    cv->setTextSize(1);
    cv->setTextColor(C_GREY);
    cv->setCursor(52, 84);
    cv->print("surveillance detection");
    tft.drawRGBBitmap(0, 0, cv->getBuffer(), W, H);
    gLastPaint = millis();
}

void tftUiSetMode(int modeIndex, const char* name, const char* desc) {
    gModeIndex = modeIndex;
    if (name) strlcpy(gModeName, name, sizeof(gModeName));
    gInSelector = (modeIndex == 0);   // mode 0 == boot selector -> show menu
    gLastRev = 0xFFFFFFFF;  // force a repaint on next tick
    (void)desc;
}

int tftUiMenuCount() { return kMenuCount; }

void tftUiMenuHighlight(int index) {
    if (kMenuCount <= 0) return;
    index %= kMenuCount;
    if (index < 0) index += kMenuCount;
    gMenuSel = index;
    gMenuRev++;             // force a repaint on next tick
}

int tftUiMenuSelected() { return gMenuSel; }

void tftUiTick(uint32_t nowMs) {
    if (!cv) return;

    // 2 Hz blink phase for alert/searching animation.
    if (nowMs - gLastBlink >= 500) {
        gBlink = !gBlink;
        gLastBlink = nowMs;
    }

    // Fold the menu-highlight revision into the detection-feed revision so a
    // highlight change repaints even though the feed itself is unchanged.
    uint32_t rev = DetectionFeed::revision() + gMenuRev;
    bool dirty = (rev != gLastRev);
    // Also refresh at least twice a second so the blink + uptime advance.
    bool periodic = (nowMs - gLastPaint >= 500);

    if (!dirty && !periodic) return;

    gLastRev = rev;
    gLastPaint = nowMs;
    render(nowMs);
}

#endif // OUISPY_HAS_TFT
