/*
 * Mode 2: OUI Spy Foxhunter
 * Single-target RSSI proximity tracker with real-time beeping.
 * Wraps the original foxhunter firmware in an anonymous namespace.
 */

// All includes from the original foxhunter (outside namespace)
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <esp_wifi.h>
#include "modes.h"
#include "detection_feed.h"

// Rename setup/loop
#define setup foxhunter_ns_setup
#define loop  foxhunter_ns_loop

namespace {
#include "raw/foxhunter.cpp"
} // anonymous namespace

#undef setup
#undef loop

void foxhunter_setup() {
    ouispy_mode_preamble("MODE 2 FOXHUNTER");
    foxhunter_ns_setup();
    ouispy_log_ap_state("MODE 2 FOXHUNTER", /*expectAP=*/true);
}
void foxhunter_loop()  { foxhunter_ns_loop(); }
void foxhunter_stop()  { /* Stage 1: end web server, stop BLE scan, silence buzzer */ }
