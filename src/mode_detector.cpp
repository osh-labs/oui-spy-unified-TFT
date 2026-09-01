/*
 * Mode 1: OUI Spy Detector
 * WiFi & BLE surveillance device scanner with web configuration.
 * Wraps the original detector firmware in an anonymous namespace.
 */

// All includes from the original detector (outside namespace for correct linkage)
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEUtils.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <vector>
#include <algorithm>
#include <FS.h>
#include <SPIFFS.h>
#include <Adafruit_NeoPixel.h>
#include "modes.h"
#include "detection_feed.h"

// Rename setup/loop to avoid conflict with Arduino entry points
#define setup detector_ns_setup
#define loop  detector_ns_loop

// Anonymous namespace: all symbols get internal linkage (no linker conflicts)
namespace {
#include "raw/detector.cpp"
} // anonymous namespace

#undef setup
#undef loop

// Exported mode entry points (called from main.cpp)
void detector_setup() {
    ouispy_mode_preamble("MODE 1 DETECTOR");
    detector_ns_setup();
    ouispy_log_ap_state("MODE 1 DETECTOR", /*expectAP=*/true);
}
void detector_loop()  { detector_ns_loop(); }
void detector_stop() {
    // The mode's globals live in this file's anonymous namespace, so they are
    // reachable unqualified here. Stop mode-specific resources; the manager's
    // releaseRadios() then deinits NimBLE and powers WiFi down after we return.
    if (pBLEScan) {
        pBLEScan->stop();
        pBLEScan->clearResults();
    }
    detectorDNS.stop();
    server.end();
    strip.clear();
    strip.show();
}
