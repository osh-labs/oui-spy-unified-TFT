/*
 * Mode 5: Sky Spy - Open Drone ID Detector
 * Monitors WiFi and BLE for FAA Remote ID broadcasts from drones.
 * Ported from classic ESP32 BLE to NimBLE via compatibility macros.
 * Wraps the original Sky-Spy firmware in an anonymous namespace.
 */

// All includes (outside namespace) - NimBLE instead of classic BLE
#include <Arduino.h>
#include <HardwareSerial.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "opendroneid.h"
#include "odid_wifi.h"
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>
#include "modes.h"
#include "detection_feed.h"

// Rename setup/loop
#define setup skyspy_ns_setup
#define loop  skyspy_ns_loop

namespace {
#include "raw/skyspy.cpp"
} // anonymous namespace

#undef setup
#undef loop

void skyspy_setup() {
    // Mode 5 has NO AP (WIFI_STA + promiscuous for Remote-ID capture), but
    // still touches the radio, so we run the same preamble.
    ouispy_mode_preamble("MODE 5 SKY SPY");
    skyspy_ns_setup();
    ouispy_log_ap_state("MODE 5 SKY SPY", /*expectAP=*/false);
}
void skyspy_loop()  { skyspy_ns_loop(); }
void skyspy_stop()  { /* Stage 1: stop BLE scan + WiFi action-frame capture */ }
