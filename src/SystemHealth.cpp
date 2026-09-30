#include "App.h"
#include <cmath>
#include <cstring>
#include <Ticker.h>
#include <atomic>

namespace {
Ticker statusTicker;
std::atomic<int> networkLedState{0}; // 0 boot, 1 no network, 2 no MQTT, 3 online, 4 AP
void renderStatusLed() {
  const unsigned long now = millis();
  bool red = false, green = false, blue = false;
  if (isBleConnected()) { blue = true; }
  else if (isBleAdvertising()) { blue = (now % 1200 < 150 || (now % 1200 >= 300 && now % 1200 < 450)); }
  else switch (networkLedState.load()) {
    case 0: red = green = blue = now % 400 < 200; break;
    case 1: red = now % 1000 < 500; break;
    case 2: red = green = now % 1000 < 500; break;
    case 3: green = true; break;
    case 4: red = blue = now % 400 < 200; break;
  }
  // Nano RGB LED is active-low. D13/LED_BUILTIN is SPI SCK: leave it alone.
  digitalWrite(LED_RED, red ? LOW : HIGH);
  digitalWrite(LED_GREEN, green ? LOW : HIGH);
  digitalWrite(LED_BLUE, blue ? LOW : HIGH);
}
unsigned long lastHeartbeat = 0;

float readNtc() {
  const int raw = analogRead(A0);
  if (raw == 0 || raw >= 4095) return 0.0F;
  const float resistance = 10000.0F * ((4095.0F / raw) - 1.0F);
  float steinhart = log(resistance / 10000.0F);
  steinhart /= 3950.0F;
  steinhart += 1.0F / (25.0F + 273.15F);
  return 1.0F / steinhart - 273.15F;
}
}

float readBoardTemperature() { return readNtc(); }

void setupStatusLed() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  renderStatusLed();
  statusTicker.attach_ms(50, renderStatusLed);
  appLog("LED", "Status LED pins configured", DEBUG);
}

void blinkConfigLed() { networkLedState = 4; }

void disableStatusLedBriefly() {
  // Status indications remain visible during network activity.
}

void updateStatusLed(bool wifiConnected, bool mqttConnected) {
  networkLedState = !wifiConnected ? 1 : !mqttConnected ? 2 : 3;
}

void saveCrashLog(const char* reason) {
  appLog("HEALTH", "Saving crash record: " + String(reason), WARN);
  crashLog.lastFreeHeap = ESP.getFreeHeap();
  crashLog.lastMinFreeHeap = ESP.getMinFreeHeap();
  crashLog.lastUptime = millis() / 1000;
  strncpy(crashLog.reason, reason, sizeof(crashLog.reason) - 1);
  crashLog.reason[sizeof(crashLog.reason) - 1] = '\0';
  preferences.begin("sys", false);
  preferences.putBytes("lastCrash", &crashLog, sizeof(crashLog));
  preferences.end();
  appLog("HEALTH", "Crash record saved");
}

void checkAndReportCrash() {
  appLog("HEALTH", "Checking for previous crash record", DEBUG);
  preferences.begin("sys", false);
  if (!preferences.isKey("lastCrash")) {
    preferences.end();
    appLog("HEALTH", "No previous crash record", DEBUG);
    return;
  }
  CrashData previous;
  preferences.getBytes("lastCrash", &previous, sizeof(previous));
  StaticJsonDocument<256> report;
  report["reason"] = previous.reason;
  report["free_heap_at_crash"] = previous.lastFreeHeap;
  report["min_free_heap"] = previous.lastMinFreeHeap;
  report["uptime_at_crash"] = previous.lastUptime;
  report["status"] = "recovered";
  String output;
  serializeJson(report, output);
  const bool published =
      mqttClient.publish(getTopic("system/crash_report"), output);
  appLog("HEALTH", "Previous crash report publish success=" +
                       String(published),
         published ? INFO : ERROR);
  preferences.remove("lastCrash");
  preferences.end();
  appLog("HEALTH", "Previous crash record cleared");
}

void processHeartbeat() {
  if (!mqttClient.isMqttConnected() || millis() - lastHeartbeat <= 2000) return;
  lastHeartbeat = millis();
  const float temperature = readNtc();
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 15000) {
    appLog("HEALTH", "Free heap below 15000 bytes; emergency restart", ERROR);
    saveCrashLog("Low Memory Warning");
    delay(1000);
    ESP.restart();
  }

  StaticJsonDocument<768> diagnostics;
  diagnostics["temp"] = String(temperature, 1);
  diagnostics["free_heap"] = freeHeap;
  diagnostics["min_free_heap"] = ESP.getMinFreeHeap();
  diagnostics["cpu_freq"] = ESP.getCpuFreqMHz();
  diagnostics["uptime"] = millis() / 1000;
  diagnostics["network"] = mqttClient.networkName();
  diagnostics["network_status"] = mqttClient.networkStatus();
  diagnostics["ip"] = mqttClient.ipAddress();
  diagnostics["signal"] = mqttClient.signalQuality();
  diagnostics["provisioning_mode"] = provisioningMode;
  diagnostics["legacy_provisioning"] = legacyProvisioningEnabled();
  diagnostics["self_service_onboarding"] = selfServiceOnboardingEnabled();
  diagnostics["uid"] = mqttClientName;
  diagnostics["mqtt_client_id"] = mqttClientName;
  diagnostics["lora_queue_size"] = loraQueue.size();
  diagnostics["lora_queue_max"] = LORA_QUEUE_MAX_SIZE;
  diagnostics["lora_busy"] = loraRequestBusy;
  diagnostics["lora_active_node_id"] = loraActiveNodeId;
  diagnostics["lora_active_request_ms"] =
      loraRequestBusy ? millis() - loraActiveRequestStartedMs : 0;
  diagnostics["lora_queue_peak"] = loraQueuePeak;
  diagnostics["lora_received_requests"] = loraReceivedRequests;
  diagnostics["lora_accepted_requests"] = loraAcceptedRequests;
  diagnostics["lora_coalesced_requests"] = loraCoalescedRequests;
  diagnostics["lora_dropped_requests"] = loraDroppedRequests;
  diagnostics["lora_expired_requests"] = loraExpiredRequests;
  diagnostics["lora_backoff_deferrals"] = loraBackoffDeferrals;
  String output;
  serializeJson(diagnostics, output);
  mqttClient.publish(getTopic("online"), "1");
  mqttClient.publish(getTopic("uid"), mqttClientName, true);
  mqttClient.publish(getTopic("version"), firmwareVersion, true);
  mqttClient.publish(getTopic("device_info"), deviceInfo, true);
  mqttClient.publish(getTopic("temperature"), String(temperature, 1));
  mqttClient.publish(getTopic("system/diag"), output);
  publishProvisioningStatus("heartbeat");

  // Publish one categorized snapshot so MQTT consumers can see every device
  // known from requests, rather than only the last status written to the
  // legacy system/device/status topic.
  static DynamicJsonDocument inventory(8192);
  inventory.clear();
  inventory["gateway_uptime_ms"] = millis();
  inventory["provisioning_mode"] = provisioningMode;
  inventory["self_service_onboarding"] = selfServiceOnboardingEnabled();
  inventory["uid"] = mqttClientName;
  inventory["mqtt_client_id"] = mqttClientName;
  JsonArray loraDevices = inventory.createNestedArray("lora_devices");
  appendLoraDevices(loraDevices);
  JsonArray modbusDevices = inventory.createNestedArray("modbus_devices");
  appendModbusDevices(modbusDevices);
  JsonObject digitalInputs = inventory.createNestedObject("digital_inputs");
  appendDigitalInputs(digitalInputs);
  JsonObject analogInputs = inventory.createNestedObject("analog_inputs");
  appendAnalogInputs(analogInputs);
  String inventoryOutput;
  serializeJson(inventory, inventoryOutput);
  mqttClient.publish(getTopic("system/devices"), inventoryOutput);
}
