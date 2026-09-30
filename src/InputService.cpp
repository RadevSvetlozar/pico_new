#include "App.h"

namespace {
constexpr uint8_t INPUT_PINS[] = {D4, D5, D6};
constexpr size_t INPUT_PIN_COUNT = sizeof(INPUT_PINS) / sizeof(INPUT_PINS[0]);
bool inputValues[INPUT_PIN_COUNT] = {};
bool inputsRead = false;
unsigned long lastInputRequestMs = 0;

bool isAllowedInput(uint16_t pin) {
  for (size_t index = 0; index < INPUT_PIN_COUNT; ++index) {
    if (INPUT_PINS[index] == pin) return true;
  }
  return false;
}
}  // namespace

void setupInputs() {
  for (size_t index = 0; index < INPUT_PIN_COUNT; ++index) {
    pinMode(INPUT_PINS[index], INPUT_PULLUP);
  }
  appLog("INPUTS", "Digital inputs ready: D4,D5,D6");
}

void handleInputRequest(const String& payload) {
  StaticJsonDocument<768> request;
  const DeserializationError error = deserializeJson(request, payload);
  if (error) {
    appLog("INPUTS", "JSON parse failed: " + String(error.c_str()), ERROR);
    return;
  }

  const uint8_t function = request["function"] | 1;
  const uint16_t firstPin = request["address"] | request["pin"] | 0;
  const uint16_t quantity = request["quantity"] | 1;

  StaticJsonDocument<1024> response;
  response.set(request);
  response["target"] = "inputs";

  if (function != 1 || quantity == 0 || quantity > INPUT_PIN_COUNT) {
    response["status"] = "unsupported_function";
  } else {
    JsonArray data = response.createNestedArray("data");
    bool valid = true;
    for (uint16_t offset = 0; offset < quantity; ++offset) {
      const uint16_t pin = firstPin + offset;
      if (!isAllowedInput(pin)) {
        valid = false;
        break;
      }
      data.add(digitalRead(pin));
    }
    if (!valid) {
      response.remove("data");
      response["status"] = "invalid_pin";
      response["allowedPins"] = "D4,D5,D6";
    } else {
      response["status"] = "ok";
      inputsRead = true;
      lastInputRequestMs = millis();
      for (size_t index = 0; index < INPUT_PIN_COUNT; ++index) {
        inputValues[index] = digitalRead(INPUT_PINS[index]);
      }
    }
  }

  String output;
  serializeJson(response, output);
  mqttClient.publish(getTopic("inputs/response"), output);
}

void appendDigitalInputs(JsonObject inputs) {
  inputs["status"] = "configured";
  inputs["last_request_ms"] = lastInputRequestMs;
  JsonArray channels = inputs.createNestedArray("channels");
  for (size_t index = 0; index < INPUT_PIN_COUNT; ++index) {
    JsonObject channel = channels.createNestedObject();
    channel["pin"] = String("D") + String(4 + index);
    if (inputsRead) channel["value"] = inputValues[index] ? 1 : 0;
  }
}
