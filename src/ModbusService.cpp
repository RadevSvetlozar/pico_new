#include "App.h"
#include <HTTPClient.h>

namespace {
struct ModbusDeviceHealth {
  bool known = false;
  bool online = false;
  uint8_t lastCode = 0;
  unsigned long lastRequestMs = 0;
  unsigned long lastSeenMs = 0;
};

ModbusDeviceHealth modbusDevices[256];

void recordModbusDevice(uint8_t slaveId, uint8_t result, bool onboardingTest) {
  if (onboardingTest || slaveId == 0) return;
  ModbusDeviceHealth& device = modbusDevices[slaveId];
  // A failed request alone is not evidence that a device exists.
  if (!device.known && result != modbusNode.ku8MBSuccess) return;
  device.known = true;
  device.online = result == modbusNode.ku8MBSuccess;
  device.lastCode = result;
  device.lastRequestMs = millis();
  if (device.online) device.lastSeenMs = millis();
}
}  // namespace

void handleModbusRequest(const String& payload) {
  appLog("MODBUS", "Request received, bytes=" + String(payload.length()),
         DEBUG);
  StaticJsonDocument<1024> request;
  const DeserializationError parseError = deserializeJson(request, payload);
  if (parseError) {
    appLog("MODBUS", "JSON parse failed: " + String(parseError.c_str()), ERROR);
    return;
  }

  const uint8_t slaveId = request["slaveId"];
  const uint8_t function = request["function"];
  const uint16_t registerAddress = request["address"];
  const uint16_t quantity = request["quantity"] | 1;
  const char* dataType = request["dataType"] | "raw";
  const float scale = request["scale"] | 1.0F;
  appLog("MODBUS", "Executing slave=" + String(slaveId) +
                        ", function=" + String(function) +
                        ", address=" + String(registerAddress) +
                        ", quantity=" + String(quantity) +
                        ", type=" + String(dataType) +
                        ", scale=" + String(scale, 3));

  StaticJsonDocument<1024> response;
  response.set(request);
  modbusNode.begin(slaveId, Serial1);
  uint8_t result = 0;

  if (function == 1 || function == 2) {
    result = function == 1
                 ? modbusNode.readCoils(registerAddress, quantity)
                 : modbusNode.readDiscreteInputs(registerAddress, quantity);
    if (result == modbusNode.ku8MBSuccess) {
      JsonArray data = response.createNestedArray("data");
      for (uint16_t i = 0; i < quantity; ++i) {
        // FC01/FC02 pack 16 channel states into each response-buffer word.
        const uint16_t packed = modbusNode.getResponseBuffer(i / 16);
        data.add((packed >> (i % 16)) & 1U);
      }
    }
  } else if (function == 3 || function == 4) {
    result = function == 3
                 ? modbusNode.readHoldingRegisters(registerAddress, quantity)
                 : modbusNode.readInputRegisters(registerAddress, quantity);
    if (result == modbusNode.ku8MBSuccess) {
      JsonArray data = response.createNestedArray("data");
      const bool twoRegisters =
          strcmp(dataType, "float") == 0 ||
          strcmp(dataType, "int32") == 0 ||
          strcmp(dataType, "uint32") == 0;
      for (uint16_t i = 0; i < quantity; i += twoRegisters ? 2 : 1) {
        if (twoRegisters && i + 1 >= quantity) break;
        if (strcmp(dataType, "float") == 0) {
          const uint32_t raw =
              (static_cast<uint32_t>(modbusNode.getResponseBuffer(i)) << 16) |
              modbusNode.getResponseBuffer(i + 1);
          float value;
          memcpy(&value, &raw, sizeof(value));
          data.add(value * scale);
        } else if (strcmp(dataType, "int16") == 0) {
          data.add(static_cast<int16_t>(modbusNode.getResponseBuffer(i)) * scale);
        } else if (strcmp(dataType, "int32") == 0) {
          const uint32_t raw =
              (static_cast<uint32_t>(modbusNode.getResponseBuffer(i)) << 16) |
              modbusNode.getResponseBuffer(i + 1);
          data.add(static_cast<int32_t>(raw) * scale);
        } else if (strcmp(dataType, "uint32") == 0) {
          const uint32_t raw =
              (static_cast<uint32_t>(modbusNode.getResponseBuffer(i)) << 16) |
              modbusNode.getResponseBuffer(i + 1);
          data.add(raw * scale);
        } else {
          data.add(modbusNode.getResponseBuffer(i) * scale);
        }
      }
    }
  } else if (function == 5) {
    result = modbusNode.writeSingleCoil(registerAddress, request["value"]);
  } else if (function == 6) {
    result = modbusNode.writeSingleRegister(registerAddress, request["value"]);
  } else if (function == 16) {
    JsonArray values = request["values"].as<JsonArray>();
    for (size_t i = 0; i < values.size(); ++i) {
      modbusNode.setTransmitBuffer(i, values[i]);
    }
    result = modbusNode.writeMultipleRegisters(registerAddress, values.size());
  } else {
    response["status"] = "unsupported_function";
    appLog("MODBUS", "Unsupported function=" + String(function), WARN);
    String output;
    serializeJson(response, output);
    mqttClient.publish(getTopic("modbus/response"), output);
    return;
  }

  response["status"] =
      result == modbusNode.ku8MBSuccess ? "ok" : "error";
  recordModbusDevice(slaveId, result, request["onboarding_test"] | false);
  if (result != modbusNode.ku8MBSuccess) {
    response["code"] = result;
    appLog("MODBUS", "Transaction failed, code=" + String(result), ERROR);
  } else {
    appLog("MODBUS", "Transaction completed successfully");
  }
  String output;
  serializeJson(response, output);
  const String topic = getTopic("modbus/response");
  const bool published = mqttClient.publish(topic, output);
  appLog("MODBUS", "Response publish topic=" + topic +
                        ", bytes=" + String(output.length()) +
                        ", success=" + String(published),
         published ? DEBUG : WARN);
}

void appendModbusDevices(JsonArray devices) {
  for (uint16_t slaveId = 1; slaveId < 256; ++slaveId) {
    const ModbusDeviceHealth& health = modbusDevices[slaveId];
    if (!health.known) continue;
    JsonObject device = devices.createNestedObject();
    device["slave_id"] = slaveId;
    device["status"] = health.online ? "online" : "error";
    device["last_code"] = health.lastCode;
    device["last_request_ms"] = health.lastRequestMs;
    if (health.lastSeenMs) device["last_seen_ms"] = health.lastSeenMs;
  }
}

void processFallback() {
  static unsigned long lastFallback = 0;
  if (mqttClient.isMqttConnected() || millis() - lastFallback <= 10000) return;
  lastFallback = millis();
  const String networkReason = mqttClient.networkFailureReason();
  const String mqttReason = mqttClient.mqttFailureReason();
  appLog("FALLBACK", "Activated because " +
      (!networkReason.isEmpty() ? networkReason : mqttReason) +
      "; starting local fallback cycle", WARN);

  size_t index = 0;
  for (JsonObject item : fallbackDoc.as<JsonArray>()) {
    ++index;
    if (!(item["enabled"] | false)) {
      appLog("FALLBACK", "Task " + String(index) + " disabled", DEBUG);
      continue;
    }
    if (!item.containsKey("payload")) {
      appLog("FALLBACK", "Task " + String(index) + " has no payload", ERROR);
      continue;
    }
    const char* protocol = item["protocol"] | "";
    appLog("FALLBACK", "Executing task " + String(index) +
                           ", protocol=" + protocol);
    JsonObject payloadObject = item["payload"];
    String payload;
    serializeJson(payloadObject, payload);

    if (strcmp(protocol, "modbus_rtu") == 0) {
      disableStatusLedBriefly();
      handleModbusRequest(payload);
    } else if (strcmp(protocol, "mqtt") == 0) {
      const bool published = mqttClient.publish(
          payloadObject["topic"] | "", payloadObject["message"] | "");
      appLog("FALLBACK", "MQTT fallback publish success=" + String(published),
             published ? DEBUG : WARN);
    } else if (strcmp(protocol, "http") == 0) {
      const String url = payloadObject["url"] | "";
      if (!url.isEmpty()) {
        HTTPClient http;
        http.begin(url);
        const int code = http.GET();
        appLog("FALLBACK", "HTTP response code=" + String(code));
        http.end();
      }
    } else {
      appLog("FALLBACK", "Unknown protocol=" + String(protocol), ERROR);
    }
  }
  appLog("FALLBACK", "Fallback cycle finished");
}
