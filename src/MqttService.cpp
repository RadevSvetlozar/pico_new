#include "App.h"

namespace {
void factoryResetMqtt(const String& payload) {
  StaticJsonDocument<384> request;
  StaticJsonDocument<256> response;
  response["command"] = "factory-reset";
  response["uid"] = mqttClientName;
  if (deserializeJson(request, payload) || !request.is<JsonObject>()) {
    response["error"] = "invalid_json";
  } else if (String(request["admin_user"] | "") != "admin" ||
             String(request["admin_password"] | "") != adminPass) {
    response["error"] = "invalid_admin_credentials";
  } else if (!request["confirm"].is<bool>() || !request["confirm"].as<bool>()) {
    response["error"] = "confirmation_required";
  } else if (beginGatewayReset("mqtt")) {
    response["pending"] = true;
    response["message"] = "Waiting for server cleanup";
  } else {
    response["error"] = "mqtt_required_or_reset_in_progress";
  }
  response["success"] = false; // Final success is sent after server cleanup + local erase.
  response["restarting"] = false;
  String output;
  serializeJson(response, output);
  mqttClient.publish(getTopic("reset/response"), output, false);
  mqttClient.publish(getTopic("factory-reset/response"), output, false);
}

bool isValidProvisioningMode(const String& mode) {
  return mode == "legacy" || mode == "self_service" || mode == "hybrid";
}

void updateProvisioningMode(const String& payload) {
  StaticJsonDocument<256> document;
  String requestedMode;
  bool restartRequested = false;
  const DeserializationError error = deserializeJson(document, payload);
  if (!error && document.is<JsonObject>()) {
    requestedMode = document["mode"] | "";
    restartRequested = document["restart"] | false;
  } else {
    requestedMode = payload;
    requestedMode.trim();
  }
  if (!isValidProvisioningMode(requestedMode)) {
    StaticJsonDocument<256> response;
    response["success"] = false;
    response["error"] = "invalid_mode";
    response["requested_mode"] = requestedMode;
    response["allowed"][0] = "legacy";
    response["allowed"][1] = "self_service";
    response["allowed"][2] = "hybrid";
    String output;
    serializeJson(response, output);
    mqttClient.publish(getTopic("system/provisioning/status"), output);
    appLog("MQTT", "Rejected invalid provisioning mode", WARN);
    return;
  }
  const String previousMode = provisioningMode;
  preferences.begin("config", false);
  preferences.putString("prov_mode", requestedMode);
  preferences.end();
  provisioningMode = requestedMode;
  appLog("MQTT", "Provisioning mode changed from " + previousMode +
                     " to " + provisioningMode);
  publishProvisioningStatus("mqtt_update");
  if (restartRequested) {
    appLog("MQTT", "Restart requested after provisioning mode update", WARN);
    delay(500);
    ESP.restart();
  }
}

void publishConfig() {
  appLog("MQTT", "Preparing configuration response");
  loadPreferences();
  StaticJsonDocument<1024> response;
  response["com1_baud_rate"] = com1BaudRate;
  response["wifi_ssid"] = wifiSsid;
  response["wifi_pass"] = wifiPass;
  response["mqtt_server"] = mqttServer;
  response["mqtt_port"] = mqttPort;
  response["mqtt_user"] = mqttUser;
  response["mqtt_pass"] = mqttPass;
  response["static_ip"] = staticIp;
  response["network_mode"] = networkMode;
  response["gsm_apn"] = gsmApn;
  response["gsm_user"] = gsmUser;
  response["gsm_pin"] = gsmPin;
  response["user_name"] = userName;
  response["address"] = address;
  response["object_name"] = objectName;
  response["device_id"] = deviceId;
  response["version"] = firmwareVersion;
  response["device_info"] = deviceInfo;
  response["uid"] = mqttClientName;
  response["mqtt_client_id"] = mqttClientName;
  response["provisioning_mode"] = provisioningMode;
  response["legacy_provisioning"] = legacyProvisioningEnabled();
  response["self_service_onboarding"] = selfServiceOnboardingEnabled();
  String output;
  serializeJson(response, output);
  const bool published =
      mqttClient.publish(getTopic("config-response"), output);
  appLog("MQTT", "Configuration response bytes=" + String(output.length()) +
                     ", publish success=" + String(published));
}

void updateConfig(const String& payload) {
  appLog("MQTT", "Configuration update received, bytes=" +
                     String(payload.length()));
  StaticJsonDocument<1024> document;
  if (deserializeJson(document, payload)) {
    mqttLog("Грешка при парсване на config JSON!", ERROR);
    return;
  }

  if (document.containsKey("target_uid") && String(document["target_uid"] | "") != mqttClientName) return;

  preferences.begin("config", false);
  preferences.putString("com1_baud_rate", document["com1_baud_rate"] | com1BaudRate);
  preferences.putString("user_name", document["user_name"] | userName);
  preferences.putString("address", document["address"] | address);
  preferences.putString("object_name", document["object_name"] | objectName);
  preferences.putString("device_id", document["device_id"] | deviceId);
  preferences.putString("ssid", document["ssid"] | wifiSsid);
  preferences.putString("pass", document["pass"] | wifiPass);
  preferences.putString("mqttip", document["mqttip"] | mqttServer);
  preferences.putString("mqttport", document["mqttport"] | mqttPort);
  preferences.putString("mqttuser", document["mqttuser"] | mqttUser);
  preferences.putString("mqttpass", document["mqttpass"] | mqttPass);
  preferences.putString("staticip", document["staticip"] | staticIp);
  preferences.putString("network_mode",
                        document["network_mode"] | networkMode);
  preferences.putString("gsm_apn", document["gsm_apn"] | gsmApn);
  preferences.putString("gsm_user", document["gsm_user"] | gsmUser);
  preferences.putString("gsm_pass", document["gsm_pass"] | gsmPass);
  preferences.putString("gsm_pin", document["gsm_pin"] | gsmPin);
  preferences.end();

  appLog("MQTT", "Configuration stored; secrets were not printed");
  appLog("MQTT", "Restarting to apply configuration");
  delay(1500);
  ESP.restart();
}
}

void onConnectionEstablished() {
  appLog("MQTT", "Connection established; registering subscriptions");
  checkAndReportCrash();
  mqttClient.publish(getTopic("uid"), mqttClientName, true);

  mqttClient.subscribe(getTopic("modbus/request"));
  mqttClient.subscribe(getTopic("inputs/request"));
  mqttClient.subscribe(getTopic("analog-inputs/request"));
  mqttClient.subscribe(getTopic("lora/scan_request"));
  mqttClient.subscribe(getTopic("lora/request"));
  mqttClient.subscribe(getTopic("restart"));
  mqttClient.subscribe(getTopic("factory-reset"));
  mqttClient.subscribe(getTopic("reset"));
  mqttClient.subscribe(getTopic("reset/ack"));
  mqttClient.subscribe(getTopic("set-fallback"));
  mqttClient.subscribe(getTopic("get-fallback"));
  mqttClient.subscribe(getTopic("get-config"));
  mqttClient.subscribe(getTopic("update-config"));
  mqttClient.subscribe(getTopic("onboarding/" + mqttClientName));
  mqttClient.subscribe(getTopic("update"));
  mqttClient.subscribe(getTopic("update-manifest"));
  mqttClient.subscribe(getTopic("system/provisioning/get"));
  mqttClient.subscribe(getTopic("system/provisioning/set"));
  publishProvisioningStatus("mqtt_connected");
  appLog("MQTT", "All subscriptions registered for base topic=" +
                     getClientId());
}

void handleMqttMessage(const String& topic, const String& payload) {
  if (topic == getTopic("reset/ack")) {
    handleGatewayResetAcknowledgement(payload);
    return;
  }
  if (gatewayResetPending()) return;
  if (topic == getTopic("modbus/request")) {
    appLog("MQTT", "RX modbus/request, bytes=" + String(payload.length()),
           DEBUG);
    handleModbusRequest(payload);
  } else if (topic == getTopic("inputs/request")) {
    appLog("MQTT", "RX inputs/request, bytes=" + String(payload.length()),
           DEBUG);
    handleInputRequest(payload);
  } else if (topic == getTopic("analog-inputs/request")) {
    appLog("MQTT", "RX analog-inputs/request, bytes=" +
                       String(payload.length()),
           DEBUG);
    handleAnalogInputRequest(payload);
  } else if (topic == getTopic("lora/scan_request")) {
    appLog("MQTT", "RX lora/scan_request");
    scanLoraNetwork(payload);
  } else if (topic == getTopic("lora/request")) {
    appLog("MQTT", "RX lora/request, bytes=" + String(payload.length()),
           DEBUG);
    queueLoraRequest(payload);
  } else if (topic == getTopic("factory-reset") || topic == getTopic("reset")) {
    factoryResetMqtt(payload);
  } else if (topic == getTopic("restart")) {
    appLog("MQTT", "RX restart; rebooting", WARN);
    delay(1000);
    ESP.restart();
  } else if (topic == getTopic("set-fallback")) {
    appLog("MQTT", "RX set-fallback, bytes=" + String(payload.length()));
    StaticJsonDocument<2048> document;
    if (deserializeJson(document, payload)) {
      mqttLog("Невалиден fallback JSON", ERROR);
      return;
    }
    String normalized;
    serializeJson(document, normalized);
    saveFallback(normalized);
  } else if (topic == getTopic("get-fallback")) {
    appLog("MQTT", "RX get-fallback");
    const bool published =
        mqttClient.publish(getTopic("fallback-state"), fallbackJson);
    appLog("MQTT", "Fallback state publish success=" + String(published),
           published ? DEBUG : WARN);
  } else if (topic == getTopic("get-config")) {
    appLog("MQTT", "RX get-config");
    publishConfig();
  } else if ((topic == getTopic("update-config") || topic == getTopic("onboarding/" + mqttClientName))) {
    appLog("MQTT", "RX update-config");
    updateConfig(payload);
  } else if (topic == getTopic("update")) {
    appLog("MQTT", "RX update, bytes=" + String(payload.length()));
    StaticJsonDocument<512> document;
    if (deserializeJson(document, payload)) {
      mqttLog("Невалиден OTA JSON", ERROR);
      return;
    }
    const String command = document["cmd"] | "";
    const String url = document["url"] | "";
    if (command == "update" && url.length() > 5) performOtaUpdate(url);
    else mqttLog("Невалидна OTA команда или URL.", ERROR);
  } else if (topic == getTopic("update-manifest")) {
    appLog("MQTT", "RX signed update manifest, bytes=" +
                       String(payload.length()));
    performOtaManifestUpdate(payload);
  } else if (topic == getTopic("system/provisioning/get")) {
    appLog("MQTT", "RX system/provisioning/get", DEBUG);
    publishProvisioningStatus("mqtt_request");
  } else if (topic == getTopic("system/provisioning/set")) {
    appLog("MQTT", "RX system/provisioning/set");
    updateProvisioningMode(payload);
  }
}

void publishProvisioningStatus(const char* source) {
  StaticJsonDocument<384> status;
  status["mode"] = provisioningMode;
  status["legacy_provisioning"] = legacyProvisioningEnabled();
  status["self_service_onboarding"] = selfServiceOnboardingEnabled();
  status["source"] = source;
  status["firmware"] = firmwareVersion;
  status["device_id"] = deviceId;
  status["uid"] = mqttClientName;
  status["uptime"] = millis() / 1000;
  status["success"] = true;
  String output;
  serializeJson(status, output);
  mqttClient.publish(getTopic("system/provisioning/status"), output);
}
