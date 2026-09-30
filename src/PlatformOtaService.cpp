#include "App.h"

#include <ArduinoOTA.h>

namespace {
bool otaReady = false;
constexpr char OTA_HOSTNAME[] = "nano-esp32";

void setupPlatformOta() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(adminPass.c_str());

  ArduinoOTA.onStart([]() {
    appLog("OTA", "PlatformIO WiFi upload started");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nPlatformIO WiFi upload completed; rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    const unsigned int percent = total == 0 ? 0 : progress / (total / 100);
    Serial.printf("\rPlatformIO OTA: %u%%", percent);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("\nPlatformIO OTA error=%u\n", error);
  });

  ArduinoOTA.begin();
  otaReady = true;
  appLog("OTA", "PlatformIO WiFi upload ready: " + String(OTA_HOSTNAME) +
                    ".local:3232");
}
}  // namespace

void processPlatformOta() {
  // ArduinoOTA uses the ESP32 WiFi interface. The existing URL-based OTA
  // remains available for WiFi, Ethernet and GSM connections.
  if (networkMode != "wifi" || WiFi.status() != WL_CONNECTED) return;
  if (!otaReady) setupPlatformOta();
  ArduinoOTA.handle();
}
