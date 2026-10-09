#include "App.h"

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <atomic>
#include <esp_system.h>
#include <freertos/task.h>
#include <freertos/queue.h>

namespace {
constexpr char SERVICE_UUID[] = "91bad492-b950-4226-aa2b-4ede9fa42f59";
constexpr char RX_UUID[] = "cba1d466-344c-4be3-ab3f-189f80dd7518";
constexpr char TX_UUID[] = "cba1d466-344c-4be3-ab3f-189f80dd7519";
constexpr unsigned long BLE_WINDOW_MS = 30000;
constexpr size_t NOTIFY_CHUNK = 180;

BLEServer* bleServer = nullptr;
BLECharacteristic* txCharacteristic = nullptr;
std::atomic<bool> bleConnected{false};
std::atomic<uint16_t> peerMtu{23};
std::atomic<bool> bleAdvertising{false};
unsigned long bleStartedAt = 0;
String commandBuffer;
struct QueuedCommand { char data[4097]; };
struct QueuedLog { char data[768]; };
QueueHandle_t commandQueue = nullptr;
QueueHandle_t logQueue = nullptr;
std::atomic<bool> receiveError{false};
bool discardingCommand = false;


void notifyLine(const String& line) {
  if (!bleConnected || txCharacteristic == nullptr) return;
  // The library lookup dereferences a peer-map entry that disconnect can remove.
  const uint16_t mtu = peerMtu.load();
  const size_t chunkSize = std::min(NOTIFY_CHUNK, size_t(mtu > 3 ? mtu - 3 : 20));
  for (size_t offset = 0; offset < line.length(); offset += chunkSize) {
    if (!bleConnected) return;
    const String chunk = line.substring(offset, offset + chunkSize);
    txCharacteristic->setValue(chunk.c_str());
    txCharacteristic->notify();
    delay(8);
  }
  if (!bleConnected) return;
  txCharacteristic->setValue("\n");
  txCharacteristic->notify();
}

void sendResponse(const char* type, JsonDocument& payload) {
  payload["type"] = type;
  String output;
  serializeJson(payload, output);
  notifyLine(output);
}

void sendError(const String& message) {
  StaticJsonDocument<192> response;
  response["message"] = message;
  sendResponse("error", response);
}

void sendConfig(bool includeSecrets = false) {
  Serial.println("[BLE] Sending config");
  DynamicJsonDocument response(includeSecrets ? 3072 : 1536);
  if (response.capacity() == 0) { sendError("Not enough memory to read configuration"); return; }
  response["network_mode"] = networkMode;
  response["ssid"] = wifiSsid;
  response["mqttip"] = mqttServer;
  response["mqttport"] = mqttPort;
  response["mqttuser"] = mqttUser;
  response["user_name"] = userName;
  response["address"] = address;
  response["object_name"] = objectName;
  response["device_id"] = deviceId;
  response["com1_baud_rate"] = com1BaudRate;
  response["staticip"] = staticIp;
  response["gsm_apn"] = gsmApn;
  response["gsm_user"] = gsmUser;
  response["firmware"] = firmwareVersion;
  response["provisioning_mode"] = provisioningMode;
  response["self_service_onboarding"] = selfServiceOnboardingEnabled();
  response["time_synchronized"] = isTimeSynchronized();
  response["time"] = currentLogTimestamp();
  if (includeSecrets) {
    response["pass"] = wifiPass;
    response["mqttpass"] = mqttPass;
    response["gsm_pass"] = gsmPass;
    response["gsm_pin"] = gsmPin;
    response["admin_pass"] = adminPass;
    response["prov_mode"] = provisioningMode;
  }
  sendResponse(includeSecrets ? "admin_authenticated" : "config", response);
}

void sendDiagnostics() {
  Serial.println("[BLE] Sending diagnostics");
  StaticJsonDocument<768> response;
  response["temperature"] = readBoardTemperature();
  response["uptime"] = millis() / 1000;
  response["free_heap"] = ESP.getFreeHeap();
  response["min_free_heap"] = ESP.getMinFreeHeap();
  response["cpu_freq"] = ESP.getCpuFreqMHz();
  response["loop_stack_free_min"] = uxTaskGetStackHighWaterMark(nullptr);
  response["reset_reason"] = static_cast<int>(esp_reset_reason());
  response["network"] = mqttClient.networkName();
  response["network_status"] = mqttClient.networkStatus();
  response["network_error"] = mqttClient.networkFailureReason();
  response["mqtt"] = mqttClient.isMqttConnected();
  response["mqtt_error"] = mqttClient.mqttFailureReason();
  response["ip"] = mqttClient.ipAddress();
  response["signal"] = mqttClient.signalQuality();
  response["device_id"] = deviceId;
  response["firmware"] = firmwareVersion;
  response["provisioning_mode"] = provisioningMode;
  const unsigned long elapsed = millis() - bleStartedAt;
  response["ble_waiting_for_mqtt"] = !mqttClient.isMqttConnected();
  response["ble_advertising"] = bleAdvertising.load();
  response["ble_connected"] = bleConnected.load();
  response["ble_window_remaining"] =
      elapsed < BLE_WINDOW_MS ? (BLE_WINDOW_MS - elapsed) / 1000 : 0;
  sendResponse("diagnostics", response);
}

String valueOr(JsonObject object, const char* key, const String& current) {
  return object.containsKey(key) ? object[key].as<String>() : current;
}

bool authenticateAdmin(JsonObject object) {
  const String user = object["admin_user"] | "";
  const String password = object["admin_password"] | "";
  if (user == "admin" && password == adminPass) return true;
  sendError("Invalid administrator credentials");
  return false;
}

void saveAdminConfig(JsonObject object) {
  if (!authenticateAdmin(object)) return;
  const String mode = object["prov_mode"] | "";
  const String password = object["admin_pass"] | "";
  const String pin = object["gsm_pin"] | "";
  if ((mode != "legacy" && mode != "self_service" && mode != "hybrid") ||
      (!password.isEmpty() && (password.length() < 8 || password.length() > 63))) {
    sendError("Invalid provisioning mode or administrator password (8-63 characters)");
    return;
  }
  preferences.begin("config", false);
  preferences.putString("prov_mode", mode);
  if (!password.isEmpty()) preferences.putString("admin_pass", password);
  if (!pin.isEmpty()) preferences.putString("gsm_pin", pin);
  preferences.end();
  StaticJsonDocument<128> response;
  response["message"] = "Administrator settings saved; restarting";
  sendResponse("admin_saved", response);
  delay(500);
  ESP.restart();
}

// Customer Wi-Fi update is available in every provisioning mode.
// Only these two fields are accepted; topic, MQTT and administrator settings stay intact.
void saveWifi(JsonObject object) {
  const String ssid = object["ssid"] | "";
  const String password = object["pass"] | "";
  if (ssid.isEmpty() || ssid.length() > 32 ||
      (!password.isEmpty() && (password.length() < 8 || password.length() > 63))) {
    sendError("SSID must be 1-32 bytes; password must be 8-63 characters"); return;
  }
  if (!preferences.begin("config", false)) { sendError("Cannot save Wi-Fi settings"); return; }
  bool saved = preferences.putString("ssid", ssid) > 0;
  if (!password.isEmpty()) saved = (preferences.putString("pass", password) > 0) && saved;
  preferences.end();
  if (!saved) { sendError("Cannot save Wi-Fi settings"); return; }
  StaticJsonDocument<128> response;
  response["message"] = "Wi-Fi settings saved; restarting";
  sendResponse("saved", response);
  delay(500);
  ESP.restart();
}

void saveConfig(JsonObject object) {
  if (!legacyProvisioningEnabled()) {
    sendError("Legacy provisioning is disabled by administrator");
    return;
  }
  if (object.containsKey("gsm_pin") && !authenticateAdmin(object)) return;
  const String newSsid = valueOr(object, "ssid", wifiSsid);
  const String newWifiPass = object["pass"] | "";
  const String newMqttPass = object["mqttpass"] | "";
  const String newGsmPass = object["gsm_pass"] | "";
  const String newSimPin = object["gsm_pin"] | "";
  const String mode = valueOr(object, "network_mode", networkMode);
  if (newSsid.isEmpty() || (!newWifiPass.isEmpty() && newWifiPass.length() < 8) ||
      (mode != "wifi" && mode != "lan" && mode != "gsm")) {
    sendError("Invalid network mode, SSID or WiFi password");
    return;
  }
  preferences.begin("config", false);
  preferences.putString("network_mode", mode);
  preferences.putString("ssid", newSsid);
  if (!newWifiPass.isEmpty()) preferences.putString("pass", newWifiPass);
  preferences.putString("mqttip", valueOr(object, "mqttip", mqttServer));
  preferences.putString("mqttport", valueOr(object, "mqttport", mqttPort));
  preferences.putString("mqttuser", valueOr(object, "mqttuser", mqttUser));
  if (!newMqttPass.isEmpty()) preferences.putString("mqttpass", newMqttPass);
  preferences.putString("user_name", valueOr(object, "user_name", userName));
  preferences.putString("address", valueOr(object, "address", address));
  preferences.putString("object_name", valueOr(object, "object_name", objectName));
  preferences.putString("device_id", valueOr(object, "device_id", deviceId));
  preferences.putString("com1_baud_rate", valueOr(object, "com1_baud_rate", com1BaudRate));
  preferences.putString("staticip", valueOr(object, "staticip", staticIp));
  preferences.putString("gsm_apn", valueOr(object, "gsm_apn", gsmApn));
  preferences.putString("gsm_user", valueOr(object, "gsm_user", gsmUser));
  if (!newGsmPass.isEmpty()) preferences.putString("gsm_pass", newGsmPass);
  if (!newSimPin.isEmpty()) preferences.putString("gsm_pin", newSimPin);
  preferences.end();
  StaticJsonDocument<128> response;
  response["message"] = "Configuration saved; restarting";
  sendResponse("saved", response);
  delay(500);
  ESP.restart();
}

void stageSelfServiceClaim(JsonObject object) {
  if (!selfServiceOnboardingEnabled()) {
    sendError("Self-service onboarding is disabled by administrator");
    return;
  }
  const String serial = object["serial_number"] | "";
  const String token = object["activation_token"] | "";
  const String ssid = object["wifi_ssid"] | "";
  const String password = object["wifi_password"] | "";
  const String apiBase = object["api_base"] | "";
  const String mqttBaseTopic = object["mqtt_base_topic"] | "";
  const String timezone = object["timezone"] | "Europe/Sofia";
  if (serial.isEmpty() || token.length() < 32 || ssid.isEmpty() ||
      (!password.isEmpty() && password.length() < 8) ||
      !apiBase.startsWith("https://") || mqttBaseTopic.isEmpty()) {
    sendError("Invalid self-service claim data");
    return;
  }
  int separators[3] = {-1, -1, -1};
  int searchFrom = 0;
  for (int i = 0; i < 3; ++i) {
    separators[i] = mqttBaseTopic.indexOf('/', searchFrom);
    if (separators[i] < 1) {
      sendError("Invalid MQTT base topic; four segments are required");
      return;
    }
    searchFrom = separators[i] + 1;
  }
  if (mqttBaseTopic.indexOf('/', searchFrom) >= 0 ||
      searchFrom >= static_cast<int>(mqttBaseTopic.length())) {
    sendError("Invalid MQTT base topic; four segments are required");
    return;
  }
  preferences.begin("config", false);
  preferences.putString("ssid", ssid);
  if (!password.isEmpty()) preferences.putString("pass", password);
  preferences.putString("claim_serial", serial);
  preferences.putString("claim_token", token);
  // The server stores this same random token in gateways.params.ota_secret.
  // The administrator password remains independent and usable by the installer.
  preferences.putString("ota_secret", token);
  preferences.putString("claim_api", apiBase);
  preferences.putString("user_name", mqttBaseTopic.substring(0, separators[0]));
  preferences.putString("address", mqttBaseTopic.substring(separators[0] + 1, separators[1]));
  preferences.putString("object_name", mqttBaseTopic.substring(separators[1] + 1, separators[2]));
  preferences.putString("device_id", mqttBaseTopic.substring(separators[2] + 1));
  preferences.putString("timezone", timezone);
  preferences.putBool("claim_pending", true);
  preferences.end();
  StaticJsonDocument<192> response;
  response["message"] = "Self-service claim staged; restarting";
  response["serial_number"] = serial;
  response["activation_pending"] = true;
  sendResponse("claim_staged", response);
  appLog("BLE", "Self-service claim staged for serial=" + serial);
  delay(500);
  ESP.restart();
}

void handleCommand(const String& input) {
  // Commands run only in loopTask; keep the JSON pool off its limited stack.
  DynamicJsonDocument document(2048);
  if (document.capacity() == 0) { sendError("Not enough memory to process BLE command"); return; }
  if (deserializeJson(document, input)) {
    sendError("Invalid JSON command");
    return;
  }
  const String command = document["command"] | "";
  if (gatewayResetPending() && command != "diagnostics" && command != "get_config") {
    sendError("Gateway reset is already in progress");
    return;
  }
  if (command == "admin_login") {
    if (!authenticateAdmin(document.as<JsonObject>())) return;
    sendConfig(true);
  }
  else if (command == "save_admin_config") saveAdminConfig(document.as<JsonObject>());
  else if (command == "get_config") sendConfig();
  else if (command == "diagnostics") sendDiagnostics();
  else if (command == "save_wifi") saveWifi(document.as<JsonObject>());
  else if (command == "save_config") saveConfig(document.as<JsonObject>());
  else if (command == "claim_device")
    stageSelfServiceClaim(document.as<JsonObject>());
  else if (command == "factory_reset") {
    if (!authenticateAdmin(document.as<JsonObject>())) return;
    if (document["confirm"] != true) {
      sendError("Factory reset requires confirmation");
      return;
    }
    if (!beginGatewayReset("bluetooth")) {
      sendError("Вече има започнат reset.");
    } else {
      StaticJsonDocument<192> response;
      response["message"] = "Свързване със сървъра и изтриване на данните…";
      sendResponse("reset_pending", response);
    }
  }
  else if (command == "restart") {
    StaticJsonDocument<96> response;
    response["message"] = "Restarting";
    sendResponse("saved", response);
    delay(300);
    ESP.restart();
  } else sendError("Unsupported command");
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    commandBuffer = "";
    discardingCommand = false;
    xQueueReset(commandQueue);
    xQueueReset(logQueue);
    peerMtu = 23;
    bleConnected = true;
    bleAdvertising = false;
    Serial.println("[BLE] GATT connected");
  }
  void onMtuChanged(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
    peerMtu = param->mtu.mtu;
  }
  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    commandBuffer = "";
    xQueueReset(commandQueue);
    xQueueReset(logQueue);
    Serial.println("[BLE] GATT disconnected");
    // Advertising is reconciled with MQTT state in the main loop.
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    const std::string value = characteristic->getValue();
    for (char character : value) {
      if (character == '\n') {
        if (!discardingCommand && !commandBuffer.isEmpty()) {
          static QueuedCommand queued{};
          commandBuffer.toCharArray(queued.data, sizeof(queued.data));
          if (xQueueSend(commandQueue, &queued, 0) != pdTRUE) receiveError = true;
        }
        commandBuffer = "";
        discardingCommand = false;
      } else if (!discardingCommand && commandBuffer.length() < 4096) {
        commandBuffer += character;
      } else {
        commandBuffer = "";
        discardingCommand = true;
        receiveError = true;
      }
    }
  }
};
}  // namespace

bool isBleConnected() { return bleConnected.load(); }
bool isBleAdvertising() { return bleAdvertising.load(); }

void setupBleProvisioning() {
  commandQueue = xQueueCreate(3, sizeof(QueuedCommand));
  logQueue = xQueueCreate(8, sizeof(QueuedLog));
  if (!commandQueue || !logQueue) {
    appLog("BLE", "Cannot allocate BLE queues", ERROR);
    return;
  }
  const uint64_t chip = ESP.getEfuseMac();
  char name[24];
  snprintf(name, sizeof(name), "NanoESP32-%04X", static_cast<uint16_t>(chip));
  BLEDevice::init(name);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());
  BLEService* service = bleServer->createService(SERVICE_UUID);
  txCharacteristic = service->createCharacteristic(
      TX_UUID, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  txCharacteristic->addDescriptor(new BLE2902());
  BLECharacteristic* rx = service->createCharacteristic(
      RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCallbacks());
  service->start();
  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();
  bleAdvertising = true;
  bleStartedAt = millis();
  appLog("BLE", "Provisioning available while MQTT offline; 30-second minimum boot window");
}

void processBleProvisioning() {
  if (!commandQueue || !logQueue) return;
  if (receiveError.exchange(false) && bleConnected) sendError("BLE command too large or queue full");
  static QueuedCommand command{};
  if (bleConnected && xQueueReceive(commandQueue, &command, 0) == pdTRUE) {
    handleCommand(String(command.data));
  }
  // Only the main loop drains this buffer.
  static QueuedLog log{};
  for (int i = 0; bleConnected && i < 2 && xQueueReceive(logQueue, &log, 0) == pdTRUE; ++i) {
    notifyLine(String(log.data));
  }
  // Never disconnect an installer when MQTT recovers.
  if (bleConnected) return;
  const bool shouldAdvertise = !mqttClient.isMqttConnected() ||
      millis() - bleStartedAt < BLE_WINDOW_MS;
  if (shouldAdvertise && !bleAdvertising) {
    BLEDevice::getAdvertising()->start();
    bleAdvertising = true;
    appLog("BLE", "Provisioning available: MQTT offline or boot window");
  } else if (!shouldAdvertise && bleAdvertising) {
    BLEDevice::getAdvertising()->stop();
    bleAdvertising = false;
    appLog("BLE", "Provisioning advertising stopped: MQTT online");
  }
}

void blePublishLog(const String& line, LogLevel level) {
  if (!bleConnected || txCharacteristic == nullptr || !logQueue) return;
  StaticJsonDocument<512> event;
  event["type"] = level == WARN || level == ERROR ? "alert_log" : "log";
  event["level"] = level == ERROR ? "ERROR" : level == WARN ? "WARN" :
                   level == DEBUG ? "DEBUG" : "INFO";
  event["message"] = line;
  String output;
  serializeJson(event, output);
  QueuedLog queued{};
  if (output.length() >= sizeof(queued.data)) return;
  output.toCharArray(queued.data, sizeof(queued.data));
  xQueueSend(logQueue, &queued, 0);
}

void notifyBleGatewayReset(bool success, const String& error) {
  if (!success) { sendError("Reset: " + error); return; }
  StaticJsonDocument<256> response;
  response["success"] = true;
  response["server_cleanup"] = true;
  response["message"] = "Gateway data and stored configuration removed; restarting";
  sendResponse("reset_complete", response);
}
