#include "App.h"

namespace {
constexpr uint8_t ANALOG_CHANNELS[] = {6, 7};
constexpr size_t ANALOG_CHANNEL_COUNT =
    sizeof(ANALOG_CHANNELS) / sizeof(ANALOG_CHANNELS[0]);
uint16_t analogValues[ANALOG_CHANNEL_COUNT] = {};
bool analogInputsRead = false;
unsigned long lastAnalogRequestMs = 0;

int adcPinForChannel(uint16_t channel) {
  switch (channel) {
    case 6: return A6;
    case 7: return A7;
    default: return -1;
  }
}
}  // namespace

void setupAnalogInputs() {
  analogReadResolution(12);
  pinMode(A6, INPUT);
  pinMode(A7, INPUT);
  appLog("ADC", "Analog inputs ready: A6,A7");
}

void handleAnalogInputRequest(const String& payload) {
  StaticJsonDocument<768> request;
  const DeserializationError error = deserializeJson(request, payload);
  if (error) {
    appLog("ADC", "JSON parse failed: " + String(error.c_str()), ERROR);
    return;
  }

  const uint8_t function = request["function"] | 1;
  const uint16_t firstChannel = request["address"] | request["channel"] | 0;
  const uint16_t quantity = request["quantity"] | 1;

  StaticJsonDocument<1024> response;
  response.set(request);
  response["target"] = "analog_inputs";
  response["adc_bits"] = 12;
  response["adc_max"] = 4095;

  if (function != 1 || quantity == 0 || quantity > 2) {
    response["status"] = "unsupported_function";
  } else {
    JsonArray data = response.createNestedArray("data");
    bool valid = true;
    for (uint16_t offset = 0; offset < quantity; ++offset) {
      const int pin = adcPinForChannel(firstChannel + offset);
      if (pin < 0) {
        valid = false;
        break;
      }
      data.add(analogRead(pin));
    }
    if (!valid) {
      response.remove("data");
      response["status"] = "invalid_pin";
      response["allowedPins"] = "A6,A7";
    } else {
      response["status"] = "ok";
      analogInputsRead = true;
      lastAnalogRequestMs = millis();
      for (size_t index = 0; index < ANALOG_CHANNEL_COUNT; ++index) {
        analogValues[index] = analogRead(adcPinForChannel(ANALOG_CHANNELS[index]));
      }
    }
  }

  String output;
  serializeJson(response, output);
  mqttClient.publish(getTopic("analog-inputs/response"), output);
}

void appendAnalogInputs(JsonObject inputs) {
  inputs["status"] = "configured";
  inputs["adc_bits"] = 12;
  inputs["adc_max"] = 4095;
  inputs["last_request_ms"] = lastAnalogRequestMs;
  JsonArray channels = inputs.createNestedArray("channels");
  for (size_t index = 0; index < ANALOG_CHANNEL_COUNT; ++index) {
    JsonObject channel = channels.createNestedObject();
    channel["pin"] = String("A") + String(ANALOG_CHANNELS[index]);
    if (analogInputsRead) channel["value"] = analogValues[index];
  }
}
