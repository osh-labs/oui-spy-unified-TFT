/*
 * detection_feed.h - Mode-agnostic detection event bus.
 *
 * Purpose
 * -------
 * The Feather TFT build renders detections graphically instead of (or in
 * addition to) beeping the buzzer. Rather than teach the TFT driver about the
 * internals of every mode, and rather than teach every mode about the TFT,
 * modes publish lightweight detection events to this global feed and the
 * display consumes a read-only snapshot of it once per render tick.
 *
 * Design
 * ------
 *  - Always compiled and near-zero cost. On the XIAO (no display) the pushes
 *    are still cheap (a memcpy into a small static ring) and simply go unread,
 *    so mode code carries a single unconditional integration path.
 *  - No dynamic allocation, no locks beyond a short critical section. Modes
 *    may push from their scan/loop context; the display reads from the main
 *    loop. Events are copied by value.
 *  - Fixed-size ring of the most recent events plus running counters and an
 *    optional "proximity" channel used by RSSI-tracking modes (Foxhunter).
 *
 * Integration contract for a mode:
 *    DetectionFeed::beginMode("FOXHUNTER", "RSSI proximity");   // on setup()
 *    DetectionFeed::setStatus(DetStatus::Scanning);             // as state changes
 *    DetectionFeed::pushDetection(evt);                         // on each hit
 *    DetectionFeed::setProximity(rssi, mac, detected);          // RSSI modes only
 *    DetectionFeed::endMode();                                  // on stop()
 */
#ifndef OUISPY_DETECTION_FEED_H
#define OUISPY_DETECTION_FEED_H

#include <stdint.h>

namespace DetectionFeed {

// Transport/classification of a detection, used to colour-code the UI.
enum class DetKind : uint8_t {
    Unknown = 0,
    BLE,        // BLE advertisement match
    WiFi,       // WiFi frame / OUI match
    Drone,      // Remote ID (Sky Spy)
    GPS,        // GPS-tagged surveillance hit (Flock-You)
    Meta,       // Meta / Ray-Ban smart glasses (composite BLE match)
    Flock,      // Flock Safety camera (WiFi OUI/probe match, tier-graded)
};

// High-level status shown in the header bar.
enum class DetStatus : uint8_t {
    Idle = 0,
    Scanning,
    Alert,      // a detection is actively being shown
    Error,
};

// One detection event. POD, copied by value into the ring.
struct DetEvent {
    char        label[24];  // device name or classification, NUL-terminated
    char        mac[18];    // "aa:bb:cc:dd:ee:ff" or "" if unknown
    int8_t      rssi;       // dBm, 0 if unknown
    uint8_t     channel;    // radio channel, 0 if n/a
    DetKind     kind;
    uint8_t     tier;       // confidence tier where the mode grades it (0 = n/a).
                            // Flock-You: 3 = wildcard probe, 4 = probe+IE sig.
    char        note[12];   // optional short annotation, "" if none. Flock-You
                            // sets it to a known common-Wi-Fi-silicon vendor
                            // name when the matched OUI is one, marking a
                            // probable false positive.
    bool        isNew;      // first sighting (true) vs. re-hit (false)
    uint32_t    ts;         // millis() at push time
};

// Immutable view handed to the display each tick.
struct Snapshot {
    char        modeName[20];
    char        modeDesc[40];
    DetStatus   status;

    uint32_t    totalHits;     // every push, including re-hits
    uint32_t    uniqueHits;    // pushes with isNew == true
    uint32_t    lastHitMs;     // millis() of most recent push (0 = none)
    float       hitsPerMin;    // decaying rate estimate

    // Most-recent-first list of recent events.
    static const int kRecent = 6;
    DetEvent    recent[kRecent];
    int         recentCount;

    // Proximity channel (Foxhunter-style RSSI tracking). Valid when
    // proximityActive is true.
    bool        proximityActive;
    bool        proximityLocked;   // target currently in range
    int8_t      proximityRssi;
    char        proximityTarget[18];
};

// ---- Producer API (called by modes) --------------------------------------

// Reset all counters/ring and set the active mode identity. Call from setup().
void beginMode(const char* name, const char* desc);

// Clear the active mode (back to idle). Call from stop().
void endMode();

// Update the header status.
void setStatus(DetStatus status);

// Publish a detection. Copies the event. Updates counters and the ring.
void pushDetection(const DetEvent& evt);

// Convenience overload for the common case. `tier` is an optional confidence
// grade (0 = not applicable); modes that rank their matches pass it so the UI
// can highlight high-confidence hits.
void pushDetection(DetKind kind, const char* label, const char* mac,
                   int8_t rssi, uint8_t channel, bool isNew, uint8_t tier = 0);

// RSSI-proximity channel (Foxhunter). Pass detected=false to show "searching".
void setProximity(int8_t rssi, const char* targetMac, bool detected);
void clearProximity();

// ---- Consumer API (called by the display) --------------------------------

// Copy the current state into `out`. Thread-safe against producers.
void getSnapshot(Snapshot& out);

// millis() of the most recent state change, so the display can skip redraws
// when nothing changed (cheap dirty check).
uint32_t revision();

} // namespace DetectionFeed

#endif // OUISPY_DETECTION_FEED_H
