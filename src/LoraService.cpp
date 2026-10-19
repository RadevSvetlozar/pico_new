#include "App.h"
#include "LoraBinaryProtocol.h"
#include <map>

namespace {
uint16_t nextRequestId = 1;
constexpr uint8_t MAX_RELAY_TTL = 3;
constexpr unsigned long DIRECT_TIMEOUT_MS = 5000;
constexpr unsigned long TIMEOUT_PER_RELAY_MS = 3500;
// A node belongs to this gateway's current inventory only while requests are
// actively routed to it. This prevents devices moved to another gateway from
// remaining in system/devices until the next reboot.
constexpr unsigned long DEVICE_INVENTORY_STALE_MS = 60000;

struct NodeHealth {
  uint8_t consecutiveFailures = 0;
  bool statusKnown = false;
  bool confirmed = false;
  bool online = false;
  uint8_t targets = 0;
  unsigned long lastRequestMs = 0;
  unsigned long lastSeenMs = 0;
  unsigned long roundTripMs = 0;
  int rssi = 0;
  float snr = 0.0F;
};

std::map<String,NodeHealth> nodeHealth;
std::map<String,unsigned long> nodeNextEligibleMs;
StaticJsonDocument<16384> topologyScanResults;
constexpr float NODE_TEMPERATURE_WARN_C = 60.0F;
constexpr float NODE_TEMPERATURE_ERROR_C = 75.0F;
uint16_t activeScanId = 0;
uint8_t activeScanTtl = 0;
unsigned long activeScanStarted = 0;
unsigned long activeScanDuration = 0;
constexpr uint8_t SCAN_ROUNDS = 3;
uint8_t activeScanRound = 0;
uint16_t scanRoundRequestIds[SCAN_ROUNDS] = {};
unsigned long scanRoundDuration = 0;

String activeScanCommandId;
String activeScanTargetUid;
unsigned long lastScanProgress = 0;
unsigned long scanIgnoredPackets = 0;
unsigned long scanReceivedReports = 0;

void publishScanEvent(const char* event, const String& message, const String& uid = "",
                      int rssi = 0, float snr = 0, bool warning = false) {
  StaticJsonDocument<768> doc;
  doc["event"] = event;
  doc["message"] = message;
  doc["scan_id"] = activeScanId;
  doc["round"] = activeScanRound + 1;
  doc["round_count"] = SCAN_ROUNDS;
  doc["command_id"] = activeScanCommandId;
  doc["gateway_uid"] = getClientId();
  doc["elapsed_ms"] = millis() - activeScanStarted;
  doc["node_count"] = topologyScanResults["nodes"].size();
  doc["reports_received"] = scanReceivedReports;
  if (uid.length()) { doc["uid"] = uid; doc["rssi"] = rssi; doc["snr"] = snr; }
  String output;
  serializeJson(doc, output);
  mqttClient.publish(getTopic("lora/scan_events"), output);
  appLog("LORA_SCAN", message, warning ? WARN : INFO);
}

String formatRadioUid(const uint8_t* bytes) {
  static const char hex[]="0123456789abcdef";
  char text[21]="promini-";
  for(uint8_t i=0;i<LoraBinary::DEVICE_UID_SIZE;++i){text[8+i*2]=hex[bytes[i]>>4];text[9+i*2]=hex[bytes[i]&15];}
  text[20]='\0';return String(text);
}
void addTopologyLink(JsonArray links, const String& from, const String& to) {
  for(JsonObject link:links)if(link["from"].as<String>()==from&&link["to"].as<String>()==to)return;
  JsonObject link=links.createNestedObject();link["from"]=from;link["to"]=to;
}

void addTopologyReport(const LoraBinary::TopologyReport& report, int rssi,
                       float snr) {
  JsonArray nodes = topologyScanResults["nodes"].as<JsonArray>();
  const String uid=formatRadioUid(report.uid);
  for(JsonObject existing:nodes)if(existing["uid"].as<String>()==uid)return;
  if (topologyScanResults.memoryUsage() + 1024 > topologyScanResults.capacity()) {
    publishScanEvent("capacity", "Scan inventory capacity reached; report omitted: " + uid, uid, rssi, snr, true);
    return;
  }
  JsonObject node=nodes.createNestedObject();node["uid"]=uid;
  node["status"] = "reachable";
  node["radio_hops"] = report.pathLength;
  node["relay_count"] = report.pathLength > 0 ? report.pathLength - 1 : 0;
  node["uptime"] = report.uptimeSeconds;
  if (report.hasDiagnostics) {
    node["free_heap"] = report.freeHeap;
    node["supply_mv"] = report.supplyMillivolts;
    node["supply_v"] = report.supplyMillivolts / 1000.0F;
  }
  if (report.hasTemperature) {
    const float temperature = report.temperatureDeciCelsius / 10.0F;
    node["temperature_c"] = temperature;
    if (temperature <= -100.0F || temperature >= 150.0F) {
      appLog("LORA", "Node=" + uid +
                         " temperature sensor reading is invalid: " +
                         String(temperature, 1) + " C",
             ERROR);
    } else if (temperature >= NODE_TEMPERATURE_ERROR_C) {
      appLog("LORA", "Node=" + uid +
                         " critical temperature=" + String(temperature, 1) +
                         " C (limit=" +
                         String(NODE_TEMPERATURE_ERROR_C, 1) + " C)",
             ERROR);
    } else if (temperature >= NODE_TEMPERATURE_WARN_C) {
      appLog("LORA", "Node=" + uid +
                         " high temperature=" + String(temperature, 1) +
                         " C (warning=" +
                         String(NODE_TEMPERATURE_WARN_C, 1) + " C)",
             WARN);
    }
  }
  node["rssi_last_hop"] = rssi;
  node["snr_last_hop"] = snr;
  JsonArray path=node.createNestedArray("path");
  JsonArray links=topologyScanResults["links"].as<JsonArray>();
  String from=getClientId();
  for(uint8_t i=0;i<report.pathLength;++i){
    const String to=formatRadioUid(report.pathUids[i]);path.add(to);addTopologyLink(links,from,to);from=to;
  }
  publishScanEvent("found", "Found " + uid + ", hops=" + String(report.pathLength) +
                   ", RSSI=" + String(rssi) + ", SNR=" + String(snr, 2), uid, rssi, snr);
}

void publishNodeHealth(const String& nodeUid, const char* status,
                       uint8_t consecutiveFailures,
                       unsigned long roundTripMs = 0, int rssi = 0,
                       float snr = 0.0F) {
  if (!mqttClient.isMqttConnected()) return;

  StaticJsonDocument<384> health;
  health["uid"] = nodeUid;
  health["status"] = status;
  health["consecutive_failures"] = consecutiveFailures;
  health["gateway_uptime_ms"] = millis();
  if (!strcmp(status, "online")) {
    health["round_trip_ms"] = roundTripMs;
    health["rssi"] = rssi;
    health["snr"] = snr;
  }

  String output;
  serializeJson(health, output);
  mqttClient.publish(getTopic("system/device/status"), output);
}

void markNodeOnline(const String& nodeUid, LoraBinary::Target target,
                    unsigned long roundTripMs, int rssi, float snr) {
  NodeHealth& health = nodeHealth[nodeUid];
  const bool recovered = health.statusKnown && !health.online;
  const bool firstSeen = !health.statusKnown;
  health.statusKnown = true;
  health.online = true;
  health.confirmed = true;
  health.consecutiveFailures = 0;
  health.targets |= static_cast<uint8_t>(1U << target);
  health.lastRequestMs = millis();
  health.lastSeenMs = millis();
  health.roundTripMs = roundTripMs;
  health.rssi = rssi;
  health.snr = snr;
  nodeNextEligibleMs[nodeUid] = 0;
  publishNodeHealth(nodeUid, "online", 0, roundTripMs, rssi, snr);

  if (recovered) {
    appLog("LORA", "Node=" + String(nodeUid) +
                       " recovered and is online, round_trip_ms=" +
                       String(roundTripMs));
  } else if (firstSeen) {
    appLog("LORA", "Node=" + String(nodeUid) + " discovered online", INFO);
  }
}

void markNodeTimeout(const String& nodeUid, LoraBinary::Target target) {
  NodeHealth& health = nodeHealth[nodeUid];
  health.targets |= static_cast<uint8_t>(1U << target);
  health.lastRequestMs = millis();
  if (health.consecutiveFailures < 255) ++health.consecutiveFailures;
  const uint8_t exponent = health.consecutiveFailures > 5
                               ? 5
                               : health.consecutiveFailures;
  const unsigned long calculatedBackoff = 1000UL << exponent;
  const unsigned long backoffMs = calculatedBackoff > 30000UL
                                      ? 30000UL
                                      : calculatedBackoff;
  nodeNextEligibleMs[nodeUid] = millis() + backoffMs;

  if (health.consecutiveFailures < 3) {
    publishNodeHealth(nodeUid, "degraded", health.consecutiveFailures);
    return;
  }

  const bool stateChanged = !health.statusKnown || health.online;
  health.statusKnown = true;
  health.online = false;
  publishNodeHealth(nodeUid, "offline", health.consecutiveFailures);
  if (stateChanged) {
    appLog("LORA", "Node=" + String(nodeUid) +
                       " marked offline after " +
                       String(health.consecutiveFailures) +
                       " consecutive timeouts",
           ERROR);
  }
}

LoraBinary::DataType parseDataType(const char* value) {
  if (!strcmp(value, "float")) return LoraBinary::FLOAT32;
  if (!strcmp(value, "int16")) return LoraBinary::INT16;
  if (!strcmp(value, "int32")) return LoraBinary::INT32;
  if (!strcmp(value, "uint32")) return LoraBinary::UINT32;
  return LoraBinary::RAW;
}

bool parseTarget(const char* value, LoraBinary::Target& target) {
  // Both routing_path forms are accepted. "lora" describes the transport,
  // while the suffix describes the service executed by the Pro Mini.
  if (!value || !strcmp(value, "modbus") || !strcmp(value, "lora_modbus")) {
    target = LoraBinary::MODBUS;
  } else if (!strcmp(value, "inputs") || !strcmp(value, "lora_inputs")) {
    target = LoraBinary::INPUTS;
  } else if (!strcmp(value, "outputs") || !strcmp(value, "lora_outputs")) {
    target = LoraBinary::OUTPUTS;
  } else if (!strcmp(value, "can") || !strcmp(value, "lora_can")) {
    target = LoraBinary::CAN_BUS;
  } else if (!strcmp(value, "analog_inputs") ||
             !strcmp(value, "lora_analog_inputs")) {
    target = LoraBinary::ANALOG_INPUTS;
  } else {
    return false;
  }
  return true;
}

const char* targetName(LoraBinary::Target target) {
  switch (target) {
    case LoraBinary::INPUTS:
      return "inputs";
    case LoraBinary::OUTPUTS:
      return "outputs";
    case LoraBinary::CAN_BUS:
      return "can";
    case LoraBinary::ANALOG_INPUTS:
      return "analog_inputs";
    default:
      return "modbus";
  }
}

bool mapRequest(const JsonDocument& json, LoraBinary::Request& request) {

  request.requestId = nextRequestId++;
  if (nextRequestId == 0) nextRequestId = 1;
  if (!parseTarget(json["target"] | "modbus", request.target)) return false;
  request.slaveId = json["slaveId"] | 0;
  request.function = json["function"] | 0;
  request.address = json["address"] | 0;
  request.quantity = json["quantity"] | 1;
  request.dataType = parseDataType(json["dataType"] | "raw");
  request.scale = json["scale"] | 1.0F;
  const int requestedTtl = json["ttl"] | 0;
  if (requestedTtl < 0 || requestedTtl > MAX_RELAY_TTL) return false;
  request.ttl = static_cast<uint8_t>(requestedTtl);
  request.maxTtl = request.ttl;
  const char* nodeUid=json["nodeUid"]|json["node_uid"]|"";
  if(strlen(nodeUid)==20&&!strncmp(nodeUid,"promini-",8)){
    request.hasUid=true;
    for(uint8_t index=0;index<LoraBinary::DEVICE_UID_SIZE;++index){
      if (!isxdigit(static_cast<unsigned char>(nodeUid[8+index*2])) ||
          !isxdigit(static_cast<unsigned char>(nodeUid[9+index*2]))) { request.hasUid=false; break; }
      const char pair[3]={nodeUid[8+index*2],nodeUid[9+index*2],'\0'};
      char* end=nullptr;request.uid[index]=static_cast<uint8_t>(strtoul(pair,&end,16));
      if(!end||*end!='\0'){request.hasUid=false;break;}
    }
  }

  if (!request.hasUid || request.quantity == 0) {
    return false;
  }

  // The existing Modbus validation remains unchanged. Other targets are now
  // mapped and transported, and the end device returns "unsupported" until
  // their hardware service is installed.
  if (request.target != LoraBinary::MODBUS) return true;
  if (request.slaveId == 0) return false;

  if (request.function == 5 || request.function == 6) {
    request.valueCount = 1;
    request.values[0] = request.function == 5
                            ? ((json["value"] | false) ? 0xFF00 : 0x0000)
                            : json["value"].as<uint16_t>();
  } else if (request.function == 15 || request.function == 16) {
    JsonArrayConst values = json["values"].as<JsonArrayConst>();
    if (values.isNull() || values.size() == 0 ||
        values.size() > LoraBinary::MAX_VALUES) {
      return false;
    }
    request.valueCount = values.size();
    request.quantity = values.size();
    for (size_t index = 0; index < values.size(); ++index) {
      request.values[index] =
          request.function == 15
              ? (values[index].as<bool>() ? 0xFF00 : 0x0000)
              : values[index].as<uint16_t>();
    }
  } else if (request.function < 1 || request.function > 4) {
    return false;
  }
  return true;
}

void sendPacket(uint8_t* packet, size_t length) {
  LoraBinary::crypt(packet, length, LORA_SECRET_KEY);
  LoRa.idle();
  LoRa.beginPacket();
  LoRa.write(packet, length);
  const int result = LoRa.endPacket();
  appLog("LORA", "Binary packet bytes=" + String(length) +
                     ", transmission result=" + String(result),
         result ? DEBUG : ERROR);
}

size_t receivePacket(uint8_t* packet, size_t capacity) {
  size_t length = 0;
  while (LoRa.available() && length < capacity) {
    packet[length++] = LoRa.read();
  }
  LoraBinary::crypt(packet, length, LORA_SECRET_KEY);
  return length;
}

void addDecodedData(JsonDocument& output, const LoraBinary::Request& request,
                    const LoraBinary::Response& response) {
  JsonArray data = output["data"].to<JsonArray>();
  const bool twoRegisters = request.dataType == LoraBinary::FLOAT32 ||
                            request.dataType == LoraBinary::INT32 ||
                            request.dataType == LoraBinary::UINT32;
  for (uint16_t index = 0; index < response.valueCount;
       index += twoRegisters ? 2 : 1) {
    if (twoRegisters && index + 1 >= response.valueCount) break;
    if (request.dataType == LoraBinary::FLOAT32) {
      const uint32_t raw =
          (static_cast<uint32_t>(response.values[index]) << 16) |
          response.values[index + 1];
      float value;
      memcpy(&value, &raw, sizeof(value));
      data.add(value * request.scale);
    } else if (request.dataType == LoraBinary::INT16) {
      data.add(static_cast<int16_t>(response.values[index]) * request.scale);
    } else if (request.dataType == LoraBinary::INT32) {
      const uint32_t raw =
          (static_cast<uint32_t>(response.values[index]) << 16) |
          response.values[index + 1];
      data.add(static_cast<int32_t>(raw) * request.scale);
    } else if (request.dataType == LoraBinary::UINT32) {
      const uint32_t raw =
          (static_cast<uint32_t>(response.values[index]) << 16) |
          response.values[index + 1];
      data.add(raw * request.scale);
    } else {
      data.add(response.values[index] * request.scale);
    }
  }
}
}  // namespace

void setupLora() {
  appLog("LORA", "Initializing binary LoRa protocol v1");
  LoRa.setPins(10, 9, 2);
  if (!LoRa.begin(868E6)) {
    appLog("LORA", "Radio not found; gateway-only mode enabled", ERROR);
    return;
  }
  loraEnabled = true;
  LoRa.setSyncWord(0x12);
  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(62.5E3);
  LoRa.setCodingRate4(8);
  LoRa.receive();
}

void queueLoraRequest(const String& payload) {
  ++loraReceivedRequests;
  StaticJsonDocument<1024> requestJson;
  if (deserializeJson(requestJson, payload)) {
    ++loraDroppedRequests;
    appLog("LORA", "Invalid JSON rejected before queue", WARN);
    return;
  }
  const uint32_t parameterId = requestJson["parameterId"] | 0U;
  LoraParameterLoad* loadSlot = nullptr;
  if (parameterId != 0) {
    const unsigned long now = millis();
    for (auto& candidate : loraParameterLoad) {
      if (candidate.parameterId == parameterId) { loadSlot = &candidate; break; }
      if (!loadSlot && candidate.parameterId == 0) loadSlot = &candidate;
    }
    if (!loadSlot) {
      loadSlot = &loraParameterLoad[0];
      for (auto& candidate : loraParameterLoad)
        if (candidate.windowStartedMs < loadSlot->windowStartedMs) loadSlot = &candidate;
    }
    if (loadSlot->parameterId != parameterId || now - loadSlot->windowStartedMs >= 60000UL) {
      loadSlot->parameterId = parameterId;
      loadSlot->requestCount = 0;
      loadSlot->acceptedCount = 0;
      loadSlot->windowStartedMs = now;
    }
    loadSlot->requestCount++;
  }
  const bool pollingRequest = parameterId != 0 &&
                              !requestJson.containsKey("command_id") &&
                              !(requestJson["onboarding_test"] | false);

  // Multiple backend replicas may publish the same periodic poll. Enforce the
  // configured interval at the gateway as a final overload guard.
  if (pollingRequest && loadSlot) {
    const unsigned long interval = constrain(
        requestJson["polling_interval"] | 5000UL, 250UL, 86400000UL);
    const unsigned long now = millis();
    if (loadSlot->lastAcceptedMs && now - loadSlot->lastAcceptedMs < interval) {
      ++loraCoalescedRequests;
      return;
    }
    loadSlot->lastAcceptedMs = now;
  }

  // Periodic polling is state sampling. If the same parameter is already
  // waiting, another copy adds no information and can starve every other
  // parameter after a radio timeout or duplicate scheduler publication.
  if (pollingRequest) {
    for (const LoraQueuedRequest& pending : loraQueue) {
      if (pending.pollingRequest && pending.parameterId == parameterId) {
        ++loraCoalescedRequests;
        return;
      }
    }
  }
  if (loraQueue.size() >= LORA_QUEUE_MAX_SIZE) {
    ++loraDroppedRequests;
    appLog("LORA", "Queue full; unique request dropped, parameter_id=" +
                       String(parameterId), WARN);
    return;
  }
  LoraQueuedRequest queued;
  queued.payload = payload;
  queued.queuedAtMs = millis();
  queued.expiresAfterMs = constrain(requestJson["queue_ttl_ms"] | 60000UL,
                                    5000UL, 300000UL);
  queued.nodeUid = requestJson["nodeUid"] | requestJson["node_uid"] | "";
  queued.nodeUid.toLowerCase();
  if(queued.nodeUid.length()!=20||!queued.nodeUid.startsWith("promini-")){
    appLog("LORA","Request rejected: valid nodeUid is required",WARN);return;
  }
  queued.parameterId = parameterId;
  queued.pollingRequest = pollingRequest;
  const int requestedPriority = requestJson["priority"] | -1;
  const int function = requestJson["function"] | 3;
  queued.priority = requestedPriority >= 0
                        ? static_cast<uint8_t>(constrain(requestedPriority, 0, 2))
                        : ([&]() -> uint8_t {
                            if (function == 5 || function == 6 ||
                                function == 15 || function == 16) return 0;
                            return requestJson.containsKey("command_id") ? 0 : 1;
                          })();
  auto position = loraQueue.begin();
  while (position != loraQueue.end() && position->priority <= queued.priority) {
    ++position;
  }
  loraQueue.insert(position, queued);
  ++loraAcceptedRequests;
  if (loadSlot) loadSlot->acceptedCount++;
  if (loraQueue.size() > loraQueuePeak) loraQueuePeak = loraQueue.size();
}

void appendLoraParameterLoad(JsonArray target) {
  const unsigned long now = millis();
  for (const auto& slot : loraParameterLoad) {
    if (!slot.parameterId) continue;
    JsonObject item = target.createNestedObject();
    item["parameter_id"] = slot.parameterId;
    item["requests_per_minute"] =
        now - slot.windowStartedMs < 60000UL ? slot.requestCount : 0;
    item["accepted_per_minute"] =
        now - slot.windowStartedMs < 60000UL ? slot.acceptedCount : 0;
    uint16_t queued = 0;
    for (const auto& request : loraQueue)
      if (request.parameterId == slot.parameterId) queued++;
    item["queued"] = queued;
  }
}

void scanLoraNetwork(const String& payload) {
  if (!loraEnabled || isScanning) {
    publishScanEvent("rejected", !loraEnabled ? "Scan rejected: LoRa disabled" : "Scan rejected: scan already running", "", 0, 0, true);
    return;
  }
  StaticJsonDocument<384> options;
  if (payload.length() && deserializeJson(options, payload)) {
    appLog("LORA_SCAN", "Scan rejected: invalid JSON", WARN);
    return;
  }
  int requestedTtl = 2;
  requestedTtl = options["ttl"] | 2;
  if (requestedTtl < 0 || requestedTtl > MAX_RELAY_TTL) {
    appLog("LORA", "Topology scan TTL must be between 0 and 3", ERROR);
    return;
  }

  isScanning = true;
  activeScanTtl = static_cast<uint8_t>(requestedTtl);
  activeScanId = nextRequestId++;
  if (nextRequestId == 0) nextRequestId = 1;
  activeScanStarted = millis();
  scanRoundDuration = 18000UL + activeScanTtl * 2000UL;
  activeScanDuration = scanRoundDuration * SCAN_ROUNDS;
  activeScanRound = 0;
  memset(scanRoundRequestIds, 0, sizeof(scanRoundRequestIds));
  scanRoundRequestIds[0] = activeScanId;
  activeScanCommandId = String(options["command_id"] | "");
  activeScanTargetUid = String(options["node_uid"] | "");
  activeScanTargetUid.toLowerCase();
  lastScanProgress = activeScanStarted;
  scanIgnoredPackets = 0;
  scanReceivedReports = 0;
  topologyScanResults.clear();
  topologyScanResults["command_id"] = activeScanCommandId;
  topologyScanResults["scan_id"] = activeScanId;
  topologyScanResults["ttl"] = activeScanTtl;
  topologyScanResults["round_count"] = SCAN_ROUNDS;
  topologyScanResults["gateway_uid"] = getClientId();
  topologyScanResults.createNestedArray("nodes");
  topologyScanResults.createNestedArray("links");

  uint8_t packet[LoraBinary::MAX_PACKET_SIZE];
  LoraBinary::TopologyDiscovery discovery;
  discovery.requestId = activeScanId;
  discovery.ttl = activeScanTtl;
  discovery.maxTtl = activeScanTtl;
  const size_t length = LoraBinary::encodeTopologyDiscovery(
      discovery, packet, sizeof(packet));
  sendPacket(packet, length);
  LoRa.receive();
  publishScanEvent("started", "Topology scan started, id=" + String(activeScanId) +
                     ", ttl=" + String(activeScanTtl) +
                     ", duration_ms=" + String(activeScanDuration));
}

void processLoraScan() {
  if (!loraEnabled || !isScanning) return;
  if (LoRa.parsePacket()) {
    uint8_t packet[LoraBinary::MAX_PACKET_SIZE];
    const size_t length = receivePacket(packet, sizeof(packet));
    LoraBinary::TopologyReport report;
    if (LoraBinary::decodeTopologyReport(packet, length, report) &&
        ((report.requestId == scanRoundRequestIds[0]) ||
         (activeScanRound >= 1 && report.requestId == scanRoundRequestIds[1]) ||
         (activeScanRound >= 2 && report.requestId == scanRoundRequestIds[2]))) {
      ++scanReceivedReports;
      addTopologyReport(report, LoRa.packetRssi(), LoRa.packetSnr());
    } else { ++scanIgnoredPackets; }
    LoRa.receive();
  }

  if (activeScanRound + 1 < SCAN_ROUNDS &&
      millis() - activeScanStarted >= (activeScanRound + 1UL) * scanRoundDuration) {
    ++activeScanRound;
    scanRoundRequestIds[activeScanRound] = nextRequestId++;
    if (nextRequestId == 0) nextRequestId = 1;
    LoraBinary::TopologyDiscovery discovery;
    discovery.requestId = scanRoundRequestIds[activeScanRound];
    discovery.ttl = discovery.maxTtl = activeScanTtl;
    uint8_t packet[LoraBinary::MAX_PACKET_SIZE];
    const size_t length = LoraBinary::encodeTopologyDiscovery(discovery, packet, sizeof(packet));
    sendPacket(packet, length);
    LoRa.receive();
    publishScanEvent("round", "Discovery round " + String(activeScanRound + 1) + "/" + String(SCAN_ROUNDS) +
                     "; unique UIDs=" + String(topologyScanResults["nodes"].size()));
  }
  if (millis() - activeScanStarted < activeScanDuration) {
    if (millis() - lastScanProgress >= 5000) {
      lastScanProgress = millis();
      publishScanEvent("progress", "Scanning; found=" + String(topologyScanResults["nodes"].size()) +
                       ", remaining_ms=" + String(activeScanDuration - (millis() - activeScanStarted)));
    }
    return;
  }
  if (activeScanTargetUid.length()) {
    bool found = false;
    for (JsonObject node : topologyScanResults["nodes"].as<JsonArray>())
      if (node["uid"].as<String>() == activeScanTargetUid) found = true;
    topologyScanResults["target_found"] = found;
    if (!found) publishScanEvent("not_found", "No reply during this scan from " + activeScanTargetUid,
                                 activeScanTargetUid, 0, 0, true);
  }
  topologyScanResults["ignored_packets"] = scanIgnoredPackets;
  topologyScanResults["reports_received"] = scanReceivedReports;
  topologyScanResults["duration_ms"] = millis() - activeScanStarted;
  topologyScanResults["node_count"] =
      topologyScanResults["nodes"].size();
  String output;
  serializeJson(topologyScanResults, output);
  const bool published =
      mqttClient.publish(getTopic("lora/scan_results"), output);
  isScanning = false;
  LoRa.receive();
  publishScanEvent("completed", "Topology scan finished, nodes=" +
                     String(topologyScanResults["nodes"].size()) +
                     ", reports=" + String(scanReceivedReports) +
                     ", ignored_packets=" + String(scanIgnoredPackets) +
                     ", bytes=" + String(output.length()) +
                     ", published=" + String(published),
         "", 0, 0, !published);
}

void processLoraQueue() {
  if (!loraEnabled || isScanning || loraQueue.empty()) return;
  const unsigned long now = millis();
  const size_t queuedCount = loraQueue.size();
  bool foundEligible = false;
  LoraQueuedRequest queued;
  for (size_t checked = 0; checked < queuedCount; ++checked) {
    queued = loraQueue.front();
    loraQueue.pop_front();
    if (now - queued.queuedAtMs >= queued.expiresAfterMs) {
      ++loraExpiredRequests;
      appLog("LORA", "Expired queued request dropped for node=" +
                         String(queued.nodeUid), WARN);
      continue;
    }
    if (nodeNextEligibleMs[queued.nodeUid] &&
        static_cast<long>(now - nodeNextEligibleMs[queued.nodeUid]) < 0) {
      ++loraBackoffDeferrals;
      loraQueue.push_back(queued);
      continue;
    }
    foundEligible = true;
    break;
  }
  if (!foundEligible) return;
  const String originalRequest = queued.payload;

  StaticJsonDocument<1024> requestJson;
  if (deserializeJson(requestJson, originalRequest)) {
    appLog("LORA", "Queued request contains invalid JSON", ERROR);
    return;
  }

  LoraBinary::Request request;
  if (!mapRequest(requestJson, request)) {
    appLog("LORA", "Unsupported target or incomplete request", ERROR);
    return;
  }

  uint8_t packet[LoraBinary::MAX_PACKET_SIZE];
  size_t length =
      LoraBinary::encodeRequest(request, packet, sizeof(packet));
  if (!length) {
    appLog("LORA", "Request exceeds LoRa packet capacity", ERROR);
    return;
  }
  // Start before sendPacket() so the measurement includes the actual radio
  // transmission as well as the remote processing and return transmission.
  const unsigned long started = millis();
  loraRequestBusy = true;
  loraActiveNodeUid = formatRadioUid(request.uid);
  loraActiveRequestStartedMs = started;
  sendPacket(packet, length);
  LoRa.receive();

  const unsigned long responseTimeout =
      DIRECT_TIMEOUT_MS + request.maxTtl * TIMEOUT_PER_RELAY_MS;
  while (millis() - started < responseTimeout) {
    if (!LoRa.parsePacket()) {
      yield();
      continue;
    }
    length = receivePacket(packet, sizeof(packet));
    LoraBinary::Response response;
    if (!LoraBinary::decodeResponse(packet, length, response) ||
        !LoraBinary::responseMatches(request,response)) {
      continue;
    }

    const unsigned long roundTripMs = millis() - started;
    const int packetRssi = LoRa.packetRssi();
    const float packetSnr = LoRa.packetSnr();
    if (!(requestJson["onboarding_test"] | false)) {
      markNodeOnline(formatRadioUid(request.uid), request.target, roundTripMs, packetRssi,
                     packetSnr);
    }
    StaticJsonDocument<1536> finalResponse;
    finalResponse.set(requestJson);
    // Always expose the normalized value, including for legacy requests where
    // target was omitted and defaulted to Modbus.
    finalResponse.remove("nodeId");finalResponse.remove("node_id");
    finalResponse["nodeUid"]=formatRadioUid(response.uid);
    finalResponse["target"] = targetName(request.target);
    finalResponse["status"] = response.status == 0 ? "ok" :
                              response.status == 2 ? "unsupported_function" :
                                                     "error";
    finalResponse["round_trip_ms"] = roundTripMs;
    finalResponse["rssi"] = packetRssi;
    finalResponse["snr"] = packetSnr;
    if (request.maxTtl > 0) {
      finalResponse["ttl"] = request.maxTtl;
      finalResponse["hops"] = request.maxTtl - response.ttl;
      finalResponse["relayed"] = response.ttl < request.maxTtl;
    }
    if (request.target == LoraBinary::ANALOG_INPUTS) {
      finalResponse["adc_bits"] = 10;
      finalResponse["adc_max"] = 1023;
    }
    if (response.modbusCode) finalResponse["code"] = response.modbusCode;
    if (response.status == 0 && request.function <= 4) {
      addDecodedData(finalResponse, request, response);
    }
    String output;
    serializeJson(finalResponse, output);
    mqttClient.publish(getTopic("lora/response"), output);
    const String timing = "Request node=" + formatRadioUid(request.uid) +
                          ", request_id=" + String(request.requestId) +
                          ", target=" + targetName(request.target) +
                          ", round_trip_ms=" + String(roundTripMs) +
                          ", rssi=" + String(packetRssi) +
                          ", snr=" + String(packetSnr, 2);
    if (response.status == 0) {
      appLog("LORA", timing, INFO);
    } else {
      appLog("LORA", timing + ", response_status=" +
                         String(response.status) + ", modbus_code=" +
                         String(response.modbusCode),
             WARN);
    }
    LoRa.receive();
    loraRequestBusy = false;
    loraActiveNodeUid = "";
    loraActiveRequestStartedMs = 0;
    return;
  }
  const unsigned long roundTripMs = millis() - started;
  if (!(requestJson["onboarding_test"] | false)) {
    markNodeTimeout(formatRadioUid(request.uid), request.target);
  }
  // Publish a correlated failure as well. MQTT clients can now distinguish a
  // real LoRa timeout from a command that was merely accepted by the broker.
  StaticJsonDocument<1536> timeoutResponse;
  timeoutResponse.set(requestJson);
  timeoutResponse.remove("nodeId");timeoutResponse.remove("node_id");
  timeoutResponse["nodeUid"]=formatRadioUid(request.uid);
  timeoutResponse["target"] = targetName(request.target);
  timeoutResponse["status"] = "timeout";
  timeoutResponse["error"] = "no_response_from_lora_node";
  timeoutResponse["round_trip_ms"] = roundTripMs;
  String timeoutOutput;
  serializeJson(timeoutResponse, timeoutOutput);
  mqttClient.publish(getTopic("lora/response"), timeoutOutput);
  appLog("LORA", "Response timeout node=" + formatRadioUid(request.uid) +
                     ", request_id=" + String(request.requestId) +
                     ", target=" + targetName(request.target) +
                     ", ttl=" + String(request.maxTtl) +
                     ", timeout_ms=" + String(responseTimeout) +
                     ", round_trip_ms=" + String(roundTripMs),
         DEBUG);
  LoRa.receive();
  loraRequestBusy = false;
  loraActiveNodeUid = "";
  loraActiveRequestStartedMs = 0;
}

void appendLoraDevices(JsonArray devices) {
  for(auto it=nodeHealth.begin();it!=nodeHealth.end();){
    const String nodeUid=it->first;
    NodeHealth& health=it->second;
    if(health.lastRequestMs==0||millis()-health.lastRequestMs>DEVICE_INVENTORY_STALE_MS){nodeNextEligibleMs.erase(nodeUid);it=nodeHealth.erase(it);continue;}
    ++it;
    if(!health.confirmed)continue;
    JsonObject device = devices.createNestedObject();
    device["uid"] = nodeUid;
    device["status"] = health.statusKnown
                           ? (health.online ? "online" : "offline")
                           : "degraded";
    device["consecutive_failures"] = health.consecutiveFailures;
    device["last_request_ms"] = health.lastRequestMs;
    if (health.lastSeenMs) device["last_seen_ms"] = health.lastSeenMs;
    if (health.online) {
      device["round_trip_ms"] = health.roundTripMs;
      device["rssi"] = health.rssi;
      device["snr"] = health.snr;
    }

    JsonArray services = device.createNestedArray("services");
    for (uint8_t target = LoraBinary::MODBUS;
         target <= LoraBinary::ANALOG_INPUTS; ++target) {
      if (health.targets & static_cast<uint8_t>(1U << target)) {
        services.add(targetName(static_cast<LoraBinary::Target>(target)));
      }
    }
  }
}
