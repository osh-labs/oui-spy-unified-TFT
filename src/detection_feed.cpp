/*
 * detection_feed.cpp - implementation of the mode-agnostic detection bus.
 *
 * See detection_feed.h for the contract. Storage is a single file-static
 * State guarded by a portMUX critical section so producer (mode scan context)
 * and consumer (main-loop display tick) never tear a read. Everything is POD;
 * there is no heap use.
 */
#include "detection_feed.h"

#include <Arduino.h>
#include <string.h>

namespace DetectionFeed {

namespace {

struct State {
    char        modeName[20];
    char        modeDesc[40];
    DetStatus   status;

    uint32_t    totalHits;
    uint32_t    uniqueHits;
    uint32_t    lastHitMs;
    float       hitsPerMin;
    uint32_t    rateWindowStart;
    uint32_t    rateWindowCount;

    // Ring buffer: head points at the next write slot; entries stored oldest
    // -> newest, snapshot reverses into most-recent-first.
    DetEvent    ring[Snapshot::kRecent];
    int         ringHead;
    int         ringCount;

    bool        proximityActive;
    bool        proximityLocked;
    int8_t      proximityRssi;
    char        proximityTarget[18];

    volatile uint32_t rev;
};

State g = {};
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;

inline void bump() { g.rev++; }

// Exponential-ish rate estimate over a rolling 10 s window.
void updateRate(uint32_t now) {
    if (g.rateWindowStart == 0) g.rateWindowStart = now;
    g.rateWindowCount++;
    uint32_t elapsed = now - g.rateWindowStart;
    if (elapsed >= 10000) {
        float perMin = (float)g.rateWindowCount * 60000.0f / (float)elapsed;
        // Blend to smooth spikes.
        g.hitsPerMin = (g.hitsPerMin * 0.5f) + (perMin * 0.5f);
        g.rateWindowStart = now;
        g.rateWindowCount = 0;
    }
}

} // namespace

void beginMode(const char* name, const char* desc) {
    portENTER_CRITICAL(&gMux);
    memset(&g, 0, sizeof(g));
    strlcpy(g.modeName, name ? name : "", sizeof(g.modeName));
    strlcpy(g.modeDesc, desc ? desc : "", sizeof(g.modeDesc));
    g.status = DetStatus::Scanning;
    g.rev = 1;
    portEXIT_CRITICAL(&gMux);
}

void endMode() {
    portENTER_CRITICAL(&gMux);
    memset(&g, 0, sizeof(g));
    g.status = DetStatus::Idle;
    g.rev = 1;
    portEXIT_CRITICAL(&gMux);
}

void setStatus(DetStatus status) {
    portENTER_CRITICAL(&gMux);
    if (g.status != status) {
        g.status = status;
        bump();
    }
    portEXIT_CRITICAL(&gMux);
}

void pushDetection(const DetEvent& evt) {
    uint32_t now = millis();
    portENTER_CRITICAL(&gMux);
    g.ring[g.ringHead] = evt;
    if (g.ring[g.ringHead].ts == 0) g.ring[g.ringHead].ts = now;
    g.ringHead = (g.ringHead + 1) % Snapshot::kRecent;
    if (g.ringCount < Snapshot::kRecent) g.ringCount++;

    g.totalHits++;
    if (evt.isNew) g.uniqueHits++;
    g.lastHitMs = now;
    g.status = DetStatus::Alert;
    updateRate(now);
    bump();
    portEXIT_CRITICAL(&gMux);
}

void pushDetection(DetKind kind, const char* label, const char* mac,
                   int8_t rssi, uint8_t channel, bool isNew) {
    DetEvent e = {};
    strlcpy(e.label, label ? label : "", sizeof(e.label));
    strlcpy(e.mac, mac ? mac : "", sizeof(e.mac));
    e.rssi = rssi;
    e.channel = channel;
    e.kind = kind;
    e.isNew = isNew;
    e.ts = millis();
    pushDetection(e);
}

void setProximity(int8_t rssi, const char* targetMac, bool detected) {
    portENTER_CRITICAL(&gMux);
    g.proximityActive = true;
    g.proximityLocked = detected;
    g.proximityRssi = rssi;
    strlcpy(g.proximityTarget, targetMac ? targetMac : "", sizeof(g.proximityTarget));
    bump();
    portEXIT_CRITICAL(&gMux);
}

void clearProximity() {
    portENTER_CRITICAL(&gMux);
    if (g.proximityActive) {
        g.proximityActive = false;
        g.proximityLocked = false;
        bump();
    }
    portEXIT_CRITICAL(&gMux);
}

void getSnapshot(Snapshot& out) {
    portENTER_CRITICAL(&gMux);
    strlcpy(out.modeName, g.modeName, sizeof(out.modeName));
    strlcpy(out.modeDesc, g.modeDesc, sizeof(out.modeDesc));
    out.status      = g.status;
    out.totalHits   = g.totalHits;
    out.uniqueHits  = g.uniqueHits;
    out.lastHitMs   = g.lastHitMs;
    out.hitsPerMin  = g.hitsPerMin;

    // Reverse the ring into most-recent-first order.
    out.recentCount = g.ringCount;
    for (int i = 0; i < g.ringCount; i++) {
        int idx = (g.ringHead - 1 - i + Snapshot::kRecent * 2) % Snapshot::kRecent;
        out.recent[i] = g.ring[idx];
    }

    out.proximityActive = g.proximityActive;
    out.proximityLocked = g.proximityLocked;
    out.proximityRssi   = g.proximityRssi;
    strlcpy(out.proximityTarget, g.proximityTarget, sizeof(out.proximityTarget));
    portEXIT_CRITICAL(&gMux);
}

uint32_t revision() {
    return g.rev;
}

} // namespace DetectionFeed
