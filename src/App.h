#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LoRa.h>
#include <ModbusMaster.h>
#include <Preferences.h>
#include <WebServer.h>
#include <deque>
#include "NetworkService.h"

constexpr uint8_t CONFIG_PIN = 0;
constexpr uint8_t RX1_PIN = A5;
constexpr uint8_t TX1_PIN = A4;
constexpr uint8_t LORA_SECRET_KEY = 0xAA;
constexpr size_t LORA_QUEUE_MAX_SIZE = 20;
constexpr size_t LORA_PARAMETER_LOAD_SLOTS = 32;

struct LoraQueuedRequest {
  String payload;
  unsigned long queuedAtMs = 0;
  unsigned long expiresAfterMs = 60000;
  uint8_t priority = 1;
  uint8_t nodeId = 0;
  uint32_t parameterId = 0;
  bool pollingRequest = false;
};

struct LoraParameterLoad {
  uint32_t parameterId = 0;
  uint32_t requestCount = 0;
  unsigned long windowStartedMs = 0;
};

enum LogLevel { DEBUG, INFO, WARN, ERROR };

struct CrashData {
  uint32_t lastFreeHeap;
  uint32_t lastMinFreeHeap;
  uint32_t lastUptime;
  char reason[32];
};

extern Preferences preferences;
extern WebServer server;
extern ModbusMaster modbusNode;
extern NetworkMqttClient mqttClient;
extern RTC_DATA_ATTR CrashData crashLog;

extern String wifiSsid;
extern String wifiPass;
extern String mqttServer;
extern String mqttPort;
extern String mqttUser;
extern String mqttPass;
extern String staticIp;
extern String userName;
extern String address;
extern String objectName;
extern String deviceId;
extern String firmwareVersion;
extern String deviceInfo;
extern String com1BaudRate;
extern String mqttClientName;
extern String networkMode;
extern String gsmApn;
extern String gsmUser;
extern String gsmPass;
extern String gsmPin;
extern String adminPass;
extern String otaSecret;
extern String provisioningMode;
extern uint32_t gsmBaudRate;
extern String fallbackJson;
extern StaticJsonDocument<2048> fallbackDoc;

extern bool loraEnabled;
extern bool isScanning;
extern std::deque<LoraQueuedRequest> loraQueue;
extern bool loraRequestBusy;
extern uint8_t loraActiveNodeId;
extern unsigned long loraActiveRequestStartedMs;
extern size_t loraQueuePeak;
extern uint32_t loraDroppedRequests;
extern uint32_t loraExpiredRequests;
extern uint32_t loraBackoffDeferrals;
extern uint32_t loraReceivedRequests;
extern uint32_t loraAcceptedRequests;
extern uint32_t loraCoalescedRequests;
extern LoraParameterLoad loraParameterLoad[LORA_PARAMETER_LOAD_SLOTS];

String getClientId();
String getTopic(const String& suffix);
bool legacyProvisioningEnabled();
bool selfServiceOnboardingEnabled();
void appLog(const char* module, const String& message, LogLevel level = INFO);
void mqttLog(const String& message, LogLevel level = INFO);
void processTimeSynchronization();
bool isTimeSynchronized();
String currentLogTimestamp();

void loadPreferences();
bool eraseStoredConfiguration();
void clearPreferences();
void saveFallback(const String& json);
void setupAccessPoint();
void setupBleProvisioning();
void processBleProvisioning();
void blePublishLog(const String& line, LogLevel level);

// BLE advertises while MQTT is offline and for at least 30 seconds after boot.
// A connected installer keeps the session alive until it disconnects.
void setupBleProvisioning();
void processBleProvisioning();
void blePublishLog(const String& line, LogLevel level);

void saveCrashLog(const char* reason);
void checkAndReportCrash();
void processHeartbeat();
float readBoardTemperature();

void handleModbusRequest(const String& payload);
void processFallback();

void setupInputs();
void handleInputRequest(const String& payload);
void setupAnalogInputs();
void handleAnalogInputRequest(const String& payload);

void setupLora();
void scanLoraNetwork(const String& payload = "");
void processLoraScan();
void queueLoraRequest(const String& payload);
void appendLoraParameterLoad(JsonArray target);
void processLoraQueue();
void appendLoraDevices(JsonArray devices);

void appendModbusDevices(JsonArray devices);
void appendDigitalInputs(JsonObject inputs);
void appendAnalogInputs(JsonObject inputs);

void performOtaUpdate(const String& url);
void performOtaManifestUpdate(const String& payload);
void processPlatformOta();
void onConnectionEstablished();
void handleMqttMessage(const String& topic, const String& payload);
void publishProvisioningStatus(const char* source = "heartbeat");

bool isBleConnected();
bool isBleAdvertising();
void setupStatusLed();
void blinkConfigLed();
void updateStatusLed(bool wifiConnected, bool mqttConnected);
void disableStatusLedBriefly();

bool beginGatewayReset(const String& source);
bool gatewayResetPending();
void processGatewayReset();
void handleGatewayResetAcknowledgement(const String& payload);
void notifyBleGatewayReset(bool success, const String& error);
