#define TINY_GSM_MODEM_SIM7600

#include "App.h"

#include <Ethernet.h>
#include <TinyGsmClient.h>
#include <WiFiClient.h>

namespace {
constexpr uint8_t ETHERNET_CS_PIN = D7;
constexpr uint8_t ETHERNET_INT_PIN = D8;
constexpr uint8_t LORA_CS_PIN = D10;
constexpr uint8_t GSM_RX_PIN = A2;
constexpr uint8_t GSM_TX_PIN = A3;
constexpr uint8_t GSM_POWER_PIN = A1;

WiFiClient wifiClient;
EthernetClient ethernetClient;
HardwareSerial gsmSerial(2);
TinyGsm modem(gsmSerial);
TinyGsmClient gsmClient(modem);

void mqttCallback(char* topic, byte* bytes, unsigned int length) {
  String payload;
  payload.reserve(length);
  for (unsigned int index = 0; index < length; ++index) {
    payload += static_cast<char>(bytes[index]);
  }
  handleMqttMessage(String(topic), payload);
}

void buildMac(uint8_t* mac) {
  const uint64_t chip = ESP.getEfuseMac();
  mac[0] = 0x02;
  for (uint8_t index = 1; index < 6; ++index) {
    mac[index] = static_cast<uint8_t>(chip >> ((index - 1) * 8));
  }
}
}  // namespace

void NetworkMqttClient::begin() {
  client_.setServer(mqttServer.c_str(), mqttPort.toInt());
  client_.setCallback(mqttCallback);
  // Topology scans and larger LoRa responses can exceed PubSubClient's small
  // default packet buffer.
  client_.setBufferSize(4096);
  startTransport();
}

namespace {
String wifiFailureReason() {
  if (wifiSsid.isEmpty()) return "Wi-Fi SSID is not configured";
  switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL: return "Wi-Fi network not found (SSID=" + wifiSsid + ")";
    case WL_CONNECT_FAILED: return "Wi-Fi authentication failed; check the password";
    case WL_CONNECTION_LOST: return "Wi-Fi connection was lost";
    case WL_DISCONNECTED: return "Wi-Fi is disconnected or connection timed out";
    case WL_IDLE_STATUS: return "Wi-Fi is still connecting";
    default: return "Wi-Fi is not connected, status=" + String(WiFi.status());
  }
}

String mqttStateReason(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT: return "MQTT connection timed out; check server, port and network";
    case MQTT_CONNECTION_LOST: return "MQTT connection was lost";
    case MQTT_CONNECT_FAILED: return "TCP connection to MQTT broker failed";
    case MQTT_DISCONNECTED: return "MQTT client is disconnected";
    case MQTT_CONNECT_BAD_PROTOCOL: return "MQTT broker rejected the protocol version";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "MQTT broker rejected the client ID";
    case MQTT_CONNECT_UNAVAILABLE: return "MQTT broker is unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "MQTT username or password is incorrect";
    case MQTT_CONNECT_UNAUTHORIZED: return "MQTT broker denied access for this device";
    default: return "MQTT connection failed, state=" + String(state);
  }
}
}  // namespace

bool NetworkMqttClient::startTransport() {
  // Wi-Fi setup is asynchronous and must continue during BLE onboarding.
  // Defer the blocking Ethernet/GSM setup while an installer is connected.
  if (networkMode != "wifi" && isBleConnected() && !gatewayResetPending()) return false;
  lastNetworkAttempt_ = millis();
  subscriptionsReady_ = false;

  if (networkMode == "lan") {
    WiFi.disconnect(true);
    gsmClient.stop();
    pinMode(LORA_CS_PIN, OUTPUT);
    digitalWrite(LORA_CS_PIN, HIGH);
    pinMode(ETHERNET_CS_PIN, OUTPUT);
    digitalWrite(ETHERNET_CS_PIN, HIGH);
    pinMode(ETHERNET_INT_PIN, INPUT_PULLUP);
    Ethernet.init(ETHERNET_CS_PIN);
    uint8_t mac[6];
    buildMac(mac);
    const int result = Ethernet.begin(mac);
    client_.setClient(ethernetClient);
    transportStarted_ = result != 0;
    appLog("NET", transportStarted_ ? "W5500 DHCP connected"
                                     : "W5500 DHCP failed",
           transportStarted_ ? INFO : WARN);
    return transportStarted_;
  }

  if (networkMode == "gsm") {
    WiFi.disconnect(true);
    pinMode(GSM_POWER_PIN, OUTPUT);
    digitalWrite(GSM_POWER_PIN, HIGH);
    gsmSerial.begin(gsmBaudRate, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    delay(300);
    if (!modem.init()) {
      appLog("NET", "SIM7600/A7670 modem init failed", WARN);
      transportStarted_ = false;
      return false;
    }
    if (gsmPin.length()) modem.simUnlock(gsmPin.c_str());
    if (!modem.waitForNetwork(30000L)) {
      appLog("NET", "GSM network registration timeout", WARN);
      transportStarted_ = false;
      return false;
    }
    transportStarted_ =
        modem.gprsConnect(gsmApn.c_str(), gsmUser.c_str(), gsmPass.c_str());
    client_.setClient(gsmClient);
    appLog("NET", transportStarted_ ? "GSM data connected"
                                     : "GSM APN connection failed",
           transportStarted_ ? INFO : WARN);
    return transportStarted_;
  }

  networkMode = "wifi";
  gsmClient.stop();
  if (wifiSsid.isEmpty()) {
    transportStarted_ = false;
    networkFailureReason_ = "Wi-Fi SSID is not configured";
    appLog("NET", networkFailureReason_, ERROR);
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  client_.setClient(wifiClient);
  transportStarted_ = true;
  networkFailureReason_ = "Wi-Fi connection started";
  appLog("NET", "WiFi connection started for SSID=" + wifiSsid);
  return true;
}

bool NetworkMqttClient::connectMqtt() {
  if (!isNetworkConnected()) return false;
  lastMqttAttempt_ = millis();
  appLog("MQTT", "Connecting to " + mqttServer + ":" + mqttPort +
                     " through " + networkName());
  const bool connected =
      mqttUser.length()
          ? client_.connect(mqttClientName.c_str(), mqttUser.c_str(),
                            mqttPass.c_str())
          : client_.connect(mqttClientName.c_str());
  if (connected) {
    networkFailureReason_ = "";
    mqttFailureReason_ = "";
    subscriptionsReady_ = true;
    appLog("MQTT", "Connected through " + networkName());
    onConnectionEstablished();
  } else {
    mqttFailureReason_ = mqttStateReason(client_.state());
    appLog("MQTT", mqttFailureReason_ + ", state=" + String(client_.state()), WARN);
  }
  return connected;
}

void NetworkMqttClient::loop() {
  if (!isNetworkConnected()) {
    const String currentReason = networkMode == "wifi" ? wifiFailureReason() :
        (networkMode == "lan" ? "Ethernet link or DHCP is unavailable" : "GSM network or data connection is unavailable");
    if (currentReason != networkFailureReason_) {
      networkFailureReason_ = currentReason;
      appLog("NET", networkFailureReason_, WARN);
    }
    if ((networkMode == "wifi" || !isBleConnected() || gatewayResetPending()) &&
        millis() - lastNetworkAttempt_ >= 10000) startTransport();
    return;
  }
  if (!client_.connected()) {
    subscriptionsReady_ = false;
    if (millis() - lastMqttAttempt_ >= 5000) connectMqtt();
    return;
  }
  client_.loop();
}

bool NetworkMqttClient::publish(const String& topic, const String& payload,
                                bool retained) {
  return client_.connected() &&
         client_.publish(topic.c_str(), payload.c_str(), retained);
}

bool NetworkMqttClient::subscribe(const String& topic) {
  return client_.connected() && client_.subscribe(topic.c_str());
}

bool NetworkMqttClient::isMqttConnected() { return client_.connected(); }

bool NetworkMqttClient::isNetworkConnected() {
  if (networkMode == "lan") {
    return transportStarted_ && Ethernet.linkStatus() == LinkON;
  }
  if (networkMode == "gsm") {
    return transportStarted_ && modem.isNetworkConnected() &&
           modem.isGprsConnected();
  }
  return WiFi.status() == WL_CONNECTED;
}

String NetworkMqttClient::networkName() { return networkMode; }

String NetworkMqttClient::networkStatus() {
  return isNetworkConnected() ? "connected" : "disconnected";
}

String NetworkMqttClient::networkFailureReason() {
  return isNetworkConnected() ? "" : networkFailureReason_;
}

String NetworkMqttClient::mqttFailureReason() {
  if (client_.connected()) return "";
  if (!isNetworkConnected()) return "MQTT cannot connect because the network is offline";
  return mqttFailureReason_;
}

String NetworkMqttClient::ipAddress() {
  if (networkMode == "lan") return Ethernet.localIP().toString();
  if (networkMode == "gsm") return modem.localIP().toString();
  return WiFi.localIP().toString();
}

int NetworkMqttClient::signalQuality() {
  if (networkMode == "gsm" && transportStarted_) return modem.getSignalQuality();
  return networkMode == "wifi" ? WiFi.RSSI() : 0;
}
