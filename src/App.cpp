#include "App.h"

Preferences preferences;
WebServer server(80);
ModbusMaster modbusNode;
NetworkMqttClient mqttClient;
RTC_DATA_ATTR CrashData crashLog;

String wifiSsid;
String wifiPass;
String mqttServer;
String mqttPort;
String mqttUser;
String mqttPass;
String staticIp;
String userName;
String address;
String objectName;
String deviceId;
String firmwareVersion = "1.4.9";
String deviceInfo = "arduino nano esp32";
String com1BaudRate;
String mqttClientName;
String networkMode = "wifi";
String gsmApn;
String gsmUser;
String gsmPass;
String gsmPin;
String adminPass;
// Safe default: firmware updates preserve the current provisioning behavior
// until an administrator explicitly enables self-service onboarding.
String provisioningMode = "hybrid";
uint32_t gsmBaudRate = 115200;
String fallbackJson = "[]";
StaticJsonDocument<2048> fallbackDoc;

bool loraEnabled = false;
bool isScanning = false;
std::deque<LoraQueuedRequest> loraQueue;
bool loraRequestBusy = false;
uint8_t loraActiveNodeId = 0;
unsigned long loraActiveRequestStartedMs = 0;
size_t loraQueuePeak = 0;
uint32_t loraDroppedRequests = 0;
uint32_t loraExpiredRequests = 0;
uint32_t loraBackoffDeferrals = 0;
uint32_t loraReceivedRequests = 0;
uint32_t loraAcceptedRequests = 0;
uint32_t loraCoalescedRequests = 0;

String getClientId() {
  return userName + "/" + address + "/" + objectName + "/" + deviceId;
}

String getTopic(const String& suffix) {
  return getClientId() + "/" + suffix;
}

bool legacyProvisioningEnabled() {
  return provisioningMode == "legacy" || provisioningMode == "hybrid";
}

bool selfServiceOnboardingEnabled() {
  return provisioningMode == "self_service" || provisioningMode == "hybrid";
}

void appLog(const char* module, const String& message, LogLevel level) {
  const char* levelName = "INFO";
  switch (level) {
    case DEBUG: levelName = "DEBUG"; break;
    case WARN: levelName = "WARN"; break;
    case ERROR: levelName = "ERROR"; break;
    default: break;
  }
  const String formatted = "[" + currentLogTimestamp() + "][" + levelName +
                           "][" + module + "] " + message;
  Serial.println(formatted);
  blePublishLog(formatted, level);
  if (mqttClient.isMqttConnected()) {
    mqttClient.publish(getTopic("log"), formatted);
    // system/log is intentionally quiet: it contains only conditions that
    // require attention, while the legacy log topic keeps the full stream.
    if (level == WARN || level == ERROR) {
      mqttClient.publish(getTopic("system/log"), formatted);
    }
  }
}

void mqttLog(const String& message, LogLevel level) {
  appLog("APP", message, level);
}
