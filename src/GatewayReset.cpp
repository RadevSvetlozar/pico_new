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

// Server has already removed the assignment. This signed command authorizes
// local erasure without requiring the deleted gateway row for reset/ack.
#include <mbedtls/md.h>
namespace {
String releaseHmac(const String& value) {
  unsigned char digest[32];
  const mbedtls_md_info_t* info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if(!info || mbedtls_md_hmac(info,reinterpret_cast<const unsigned char*>(otaSecret.c_str()),otaSecret.length(),reinterpret_cast<const unsigned char*>(value.c_str()),value.length(),digest))return "";
  String output;output.reserve(64);
  for(uint8_t i=0;i<32;i++){char hex[3];snprintf(hex,sizeof(hex),"%02x",digest[i]);output+=hex;}
  return output;
}
}
void handleServerGatewayRelease(const String& payload) {
  StaticJsonDocument<512> request;
  if(deserializeJson(request,payload)||String(request["action"]|"")!="gateway_release"||String(request["uid"]|"")!=mqttClientName||otaSecret.length()<8)return;
  const String id=request["release_id"]|"",signature=request["signature"]|"";
  if(id.length()!=36||signature.length()!=64)return;
  const String expected=releaseHmac("gateway-release\n"+mqttClientName+"\n"+id);
  if(expected.length()!=64)return;
  uint8_t difference=0;
  for(uint8_t i=0;i<64;i++)difference|=signature[i]^expected[i];
  if(difference)return;
  const String proof=releaseHmac("gateway-release-complete\n"+mqttClientName+"\n"+id);
  // Keep a receipt outside the erased config namespace so a lost MQTT reply
  // can be resent after Wi-Fi is configured again.
  Preferences receipt;
  if(!receipt.begin("release_receipt",false))return;
  const String responseTopic=getTopic("release/response");
  const bool stored=(receipt.putBool("done",false)>0)&&receipt.putString("uid",mqttClientName)>0&&receipt.putString("id",id)>0&&receipt.putString("signature",proof)>0&&receipt.putString("topic",responseTopic)>0;
  receipt.end();
  if(!stored)return;
  const bool success=eraseStoredConfiguration();
  if(success){receipt.begin("release_receipt",false);receipt.putBool("done",true);receipt.end();}
  StaticJsonDocument<384> response;
  response["uid"]=mqttClientName;response["release_id"]=id;response["success"]=success;
  if(success)response["signature"]=proof;
  else response["error"]="configuration_clear_failed";
  String output;serializeJson(response,output);
  mqttClient.publish(getTopic("release/response"),output,false);
  if(success){delay(1000);ESP.restart();}
}

void reportGatewayReleaseReceipt() {
  Preferences receipt;
  if(!receipt.begin("release_receipt",true))return;
  if(!receipt.getBool("done",false)){receipt.end();return;}
  StaticJsonDocument<384> response;
  response["uid"]=receipt.getString("uid","");
  response["release_id"]=receipt.getString("id","");
  response["signature"]=receipt.getString("signature","");
  response["success"]=true;
  const String topic=receipt.getString("topic","");receipt.end();
  if(topic.isEmpty())return;
  String output;serializeJson(response,output);mqttClient.publish(topic,output,false);
}
