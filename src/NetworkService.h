#pragma once

#include <Arduino.h>
#include <PubSubClient.h>

class NetworkMqttClient {
 public:
  void begin();
  void loop();
  bool publish(const String& topic, const String& payload, bool retained = false);
  bool subscribe(const String& topic);
  bool isMqttConnected();
  bool isNetworkConnected();
  String networkName();
  String networkStatus();
  String networkFailureReason();
  String mqttFailureReason();
  String ipAddress();
  int signalQuality();

 private:
  bool startTransport();
  bool connectMqtt();
  PubSubClient client_;
  unsigned long lastNetworkAttempt_ = 0;
  unsigned long lastMqttAttempt_ = 0;
  bool transportStarted_ = false;
  bool subscriptionsReady_ = false;
  String networkFailureReason_ = "Not connected yet";
  String mqttFailureReason_ = "Not connected yet";
};
