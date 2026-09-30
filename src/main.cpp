#include "App.h"
#include "esp_system.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  appLog("BOOT", "Firmware boot, version=" + firmwareVersion);

  const esp_reset_reason_t resetReason = esp_reset_reason();
  appLog("BOOT", "Reset reason code=" + String(static_cast<int>(resetReason)));
  if (resetReason == ESP_RST_TASK_WDT) {
    appLog("BOOT", "Previous reset was caused by task watchdog", WARN);
    saveCrashLog("Watchdog Timeout");
  }

  pinMode(CONFIG_PIN, INPUT_PULLUP);
  appLog("BOOT", "GPIO configuration started", DEBUG);
  setupStatusLed();
  setupInputs();
  setupAnalogInputs();
  delay(2000);
  setupLora();

  appLog("BOOT", "Loading stored configuration");
  loadPreferences();
  setupBleProvisioning();
  appLog("MQTT", "Client name configured: " + mqttClientName);

  if (digitalRead(CONFIG_PIN) == LOW) {
    appLog("BOOT", "CONFIG pin is LOW; entering setup AP mode", WARN);
    setupAccessPoint();
    return;
  }

  appLog("MODBUS", "Starting Serial1 at " + com1BaudRate + " baud");
  Serial1.begin(com1BaudRate.toInt(), SERIAL_8N1, RX1_PIN, TX1_PIN);
  delay(1000);
  appLog("MQTT", "Broker configured: " + mqttServer + ":" + mqttPort +
                     ", user configured=" + String(!mqttUser.isEmpty()));
  mqttClient.begin();
  appLog("BOOT", "Initialization complete");
}

void loop() {
  static bool previousWifi = false;
  static bool previousMqtt = false;
  static bool stateInitialized = false;

  processBleProvisioning();

  if (WiFi.getMode() == WIFI_AP) {
    server.handleClient();
    blinkConfigLed();
    return;
  }

  mqttClient.loop();
  processGatewayReset();
  if (gatewayResetPending()) return;
  processTimeSynchronization();
  processPlatformOta();
  const bool wifiConnected = mqttClient.isNetworkConnected();
  const bool mqttConnected = mqttClient.isMqttConnected();
  if (!stateInitialized || wifiConnected != previousWifi) {
    appLog("NET", wifiConnected ? "Connected through " +
                                      mqttClient.networkName() + ", IP=" +
                                      mqttClient.ipAddress()
                                : "Disconnected: " + mqttClient.networkName(),
           wifiConnected ? INFO : WARN);
    previousWifi = wifiConnected;
  }
  if (!stateInitialized || mqttConnected != previousMqtt) {
    appLog("MQTT", mqttConnected ? "Connected" : "Disconnected",
           mqttConnected ? INFO : WARN);
    previousMqtt = mqttConnected;
  }
  stateInitialized = true;
  updateStatusLed(wifiConnected, mqttConnected);
  processLoraScan();
  processLoraQueue();
  processFallback();
  processHeartbeat();
}
