#include "App.h"
#include "esp_system.h"

namespace {
bool pending = false;
bool approved = false;
String requestId, sourceName, serverError;
unsigned long startedAt = 0, lastSentAt = 0;

void reportReset(bool success, const String& error = "") {
  StaticJsonDocument<512> response;
  response["uid"] = mqttClientName;
  response["command_id"] = requestId;
  response["command"] = "reset";
  response["success"] = success;
  response["restarting"] = success;
  response["server_cleanup"] = approved;
  if (!error.isEmpty()) response["error"] = error;
  String output;
  serializeJson(response, output);
  mqttClient.publish(getTopic("reset/response"), output, false);
  mqttClient.publish(getTopic("factory-reset/response"), output, false);
  if (sourceName == "bluetooth") notifyBleGatewayReset(success, error);
}
}

bool gatewayResetPending() { return pending; }

bool beginGatewayReset(const String& source) {
  // BLE can start before MQTT connects. Keep credentials until server ACK.
  if (pending || (!mqttClient.isMqttConnected() && source != "bluetooth")) return false;
  char id[33];
  snprintf(id, sizeof(id), "%08lx%08lx%08lx%08lx", (unsigned long)esp_random(),
           (unsigned long)esp_random(), (unsigned long)esp_random(), (unsigned long)esp_random());
  requestId = id;
  sourceName = source;
  pending = true;
  approved = false;
  serverError = "";
  startedAt = millis();
  lastSentAt = startedAt - 2000;
  return true;
}

void handleGatewayResetAcknowledgement(const String& payload) {
  if (!pending) return;
  StaticJsonDocument<2048> response;
  if (deserializeJson(response, payload) ||
      String(response["uid"] | "") != mqttClientName ||
      String(response["command_id"] | "") != requestId ||
      String(response["phase"] | "") != "server_reset_complete") return;
  if (response["success"] == true) approved = true;
  else serverError = response["error"] | "server_reset_failed";
}

void processGatewayReset() {
  if (!pending) return;
  if (approved) {
    // Never erase the network credentials until the server transaction has committed.
    const bool cleared = eraseStoredConfiguration();
    reportReset(cleared, cleared ? "" : "configuration_clear_failed_after_server_reset");
    pending = false;
    if (cleared) { delay(1000); ESP.restart(); }
    return;
  }
  if (!serverError.isEmpty() || millis() - startedAt >= 30000) {
    reportReset(false, serverError.isEmpty() ? "server_reset_timeout_retry_required" : serverError);
    pending = false;
    return;
  }
  if (mqttClient.isMqttConnected() && millis() - lastSentAt >= 2000) {
    lastSentAt = millis();
    StaticJsonDocument<384> request;
    request["uid"] = mqttClientName;
    request["command_id"] = requestId;
    request["action"] = "gateway_reset";
    request["source"] = sourceName;
    request["confirm"] = true;
    String output;
    serializeJson(request, output);
    mqttClient.publish(getTopic("reset/request"), output, false);
  }
}
