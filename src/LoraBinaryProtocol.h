#pragma once

#include <Arduino.h>
#include <string.h>

namespace LoraBinary {

constexpr uint8_t MAGIC = 0xA7;
constexpr uint8_t VERSION = 1;
constexpr size_t MAX_PACKET_SIZE = 255;
// ModbusMaster has a 64-word response/transmit buffer on both targets.
constexpr size_t MAX_VALUES = 64;

enum MessageType : uint8_t {
  REQUEST = 1,
  RESPONSE = 2,
  PING = 3,
  PONG = 4,
  TOPOLOGY_DISCOVERY = 5,
  TOPOLOGY_REPORT = 6,
};

constexpr uint8_t MAX_ROUTE_HOPS = 3;
constexpr uint8_t DEVICE_UID_SIZE = 6;

struct TopologyDiscovery {
  uint16_t requestId = 0;
  uint8_t ttl = 0;
  uint8_t maxTtl = 0;
  uint8_t pathLength = 0;
  uint8_t path[MAX_ROUTE_HOPS];
};

struct TopologyReport {
  uint8_t nodeId = 0;
  uint16_t requestId = 0;
  uint32_t uptimeSeconds = 0;
  uint16_t freeHeap = 0;
  uint16_t supplyMillivolts = 0;
  int16_t temperatureDeciCelsius = 0;
  bool hasDiagnostics = false;
  bool hasTemperature = false;
  bool hasUid = false;
  uint8_t uid[DEVICE_UID_SIZE] = {};
  uint8_t ttl = 0;
  uint8_t maxTtl = 0;
  uint8_t pathLength = 0;
  uint8_t path[MAX_ROUTE_HOPS + 1];
};

enum DataType : uint8_t {
  RAW = 0,
  FLOAT32 = 1,
  INT16 = 2,
  INT32 = 3,
  UINT32 = 4,
};

// Physical service that must execute the request on the end device.
// MODBUS is zero intentionally: legacy packets without a target keep the
// behaviour they had before routing was introduced.
enum Target : uint8_t {
  MODBUS = 0,
  INPUTS = 1,
  OUTPUTS = 2,
  CAN_BUS = 3,
  ANALOG_INPUTS = 4,
};

struct Request {
  uint8_t nodeId = 0;
  uint16_t requestId = 0;
  Target target = MODBUS;
  uint8_t slaveId = 0;
  uint8_t function = 0;
  uint16_t address = 0;
  uint16_t quantity = 1;
  DataType dataType = RAW;
  float scale = 1.0F;
  uint8_t valueCount = 0;
  uint16_t values[MAX_VALUES];
  // Optional relay extension. maxTtl == 0 keeps the original wire format.
  uint8_t ttl = 0;
  uint8_t maxTtl = 0;
  bool hasUid = false;
  uint8_t uid[DEVICE_UID_SIZE] = {};
};

struct Response {
  uint8_t nodeId = 0;
  uint16_t requestId = 0;
  uint8_t status = 0;
  uint8_t modbusCode = 0;
  uint16_t valueCount = 0;
  uint16_t values[MAX_VALUES];
  uint8_t ttl = 0;
  uint8_t maxTtl = 0;
};

inline void put16(uint8_t* output, size_t& offset, uint16_t value) {
  output[offset++] = static_cast<uint8_t>(value >> 8);
  output[offset++] = static_cast<uint8_t>(value);
}

inline uint16_t get16(const uint8_t* input, size_t& offset) {
  const uint16_t value =
      (static_cast<uint16_t>(input[offset]) << 8) | input[offset + 1];
  offset += 2;
  return value;
}

inline void put32(uint8_t* output, size_t& offset, uint32_t value) {
  output[offset++] = static_cast<uint8_t>(value >> 24);
  output[offset++] = static_cast<uint8_t>(value >> 16);
  output[offset++] = static_cast<uint8_t>(value >> 8);
  output[offset++] = static_cast<uint8_t>(value);
}

inline uint32_t get32(const uint8_t* input, size_t& offset) {
  const uint32_t value =
      (static_cast<uint32_t>(input[offset]) << 24) |
      (static_cast<uint32_t>(input[offset + 1]) << 16) |
      (static_cast<uint32_t>(input[offset + 2]) << 8) | input[offset + 3];
  offset += 4;
  return value;
}

inline void putFloat(uint8_t* output, size_t& offset, float value) {
  uint32_t raw;
  memcpy(&raw, &value, sizeof(raw));
  output[offset++] = static_cast<uint8_t>(raw >> 24);
  output[offset++] = static_cast<uint8_t>(raw >> 16);
  output[offset++] = static_cast<uint8_t>(raw >> 8);
  output[offset++] = static_cast<uint8_t>(raw);
}

inline float getFloat(const uint8_t* input, size_t& offset) {
  const uint32_t raw = (static_cast<uint32_t>(input[offset]) << 24) |
                       (static_cast<uint32_t>(input[offset + 1]) << 16) |
                       (static_cast<uint32_t>(input[offset + 2]) << 8) |
                       input[offset + 3];
  offset += 4;
  float value;
  memcpy(&value, &raw, sizeof(value));
  return value;
}

inline uint16_t crc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

inline void crypt(uint8_t* data, size_t length, uint8_t key) {
  for (size_t index = 0; index < length; ++index) {
    data[index] ^= static_cast<uint8_t>(key + index * 29U);
  }
}

inline bool verify(const uint8_t* data, size_t length, MessageType type) {
  if (length < 7 || data[0] != MAGIC || data[1] != VERSION ||
      data[2] != type) {
    return false;
  }
  size_t crcOffset = length - 2;
  size_t readOffset = crcOffset;
  return get16(data, readOffset) == crc16(data, crcOffset);
}

inline size_t encodeRequest(const Request& request, uint8_t* output,
                            size_t capacity) {
  const bool relayExtension = request.maxTtl > 0;
  const size_t length = 21U + request.valueCount * 2U +
                        (relayExtension ? 2U : 0U) +
                        (request.hasUid ? DEVICE_UID_SIZE : 0U);
  if (request.valueCount > MAX_VALUES || length > capacity) return 0;
  size_t offset = 0;
  output[offset++] = MAGIC;
  output[offset++] = VERSION;
  output[offset++] = REQUEST;
  output[offset++] = request.nodeId;
  put16(output, offset, request.requestId);
  output[offset++] = request.target;
  output[offset++] = request.slaveId;
  output[offset++] = request.function;
  put16(output, offset, request.address);
  put16(output, offset, request.quantity);
  output[offset++] = request.dataType;
  putFloat(output, offset, request.scale);
  output[offset++] = request.valueCount;
  for (uint8_t index = 0; index < request.valueCount; ++index) {
    put16(output, offset, request.values[index]);
  }
  if (relayExtension) {
    output[offset++] = request.ttl;
    output[offset++] = request.maxTtl;
  }
  if (request.hasUid) {
    for (uint8_t index = 0; index < DEVICE_UID_SIZE; ++index) output[offset++] = request.uid[index];
  }
  put16(output, offset, crc16(output, offset));
  return offset;
}

inline bool decodeRequest(const uint8_t* input, size_t length,
                          Request& request) {
  if (!verify(input, length, REQUEST) || length < 20) return false;
  size_t offset = 3;
  request.nodeId = input[offset++];
  request.requestId = get16(input, offset);
  // Accept the original packet layout as target=MODBUS. This makes an updated
  // end device compatible with requests sent by the previous gateway firmware.
  const bool routed = length >= 21 && input[18] <= MAX_VALUES &&
      (length == 21U + static_cast<size_t>(input[18]) * 2U ||
       length == 23U + static_cast<size_t>(input[18]) * 2U ||
       length == 27U + static_cast<size_t>(input[18]) * 2U ||
       length == 29U + static_cast<size_t>(input[18]) * 2U);
  const bool legacy =
      !routed && input[17] <= MAX_VALUES &&
      length == 20U + static_cast<size_t>(input[17]) * 2U;
  if (!routed && !legacy) return false;
  request.target = legacy ? MODBUS : static_cast<Target>(input[offset++]);
  if (request.target > ANALOG_INPUTS) return false;
  request.slaveId = input[offset++];
  request.function = input[offset++];
  request.address = get16(input, offset);
  request.quantity = get16(input, offset);
  request.dataType = static_cast<DataType>(input[offset++]);
  request.scale = getFloat(input, offset);
  request.valueCount = input[offset++];
  const size_t baseLength = (legacy ? 20U : 21U) +
                            request.valueCount * 2U;
  const size_t extensionLength=length-baseLength;
  const bool relayExtension = !legacy && (extensionLength==2U||extensionLength==2U+DEVICE_UID_SIZE);
  const bool uidExtension = !legacy && (extensionLength==DEVICE_UID_SIZE||extensionLength==2U+DEVICE_UID_SIZE);
  if (request.valueCount > MAX_VALUES ||
      (!legacy&&extensionLength!=0U&&!relayExtension&&!uidExtension)) {
    return false;
  }
  for (uint8_t index = 0; index < request.valueCount; ++index) {
    request.values[index] = get16(input, offset);
  }
  request.ttl = relayExtension ? input[offset++] : 0;
  request.maxTtl = relayExtension ? input[offset++] : 0;
  request.hasUid=uidExtension;
  if(uidExtension)for(uint8_t index=0;index<DEVICE_UID_SIZE;++index)request.uid[index]=input[offset++];
  if (request.ttl > request.maxTtl) return false;
  return true;
}

inline size_t encodeResponse(const Response& response, uint8_t* output,
                             size_t capacity) {
  const bool relayExtension = response.maxTtl > 0;
  const size_t length = 12U + response.valueCount * 2U +
                        (relayExtension ? 2U : 0U);
  if (response.valueCount > MAX_VALUES || length > capacity) return 0;
  size_t offset = 0;
  output[offset++] = MAGIC;
  output[offset++] = VERSION;
  output[offset++] = RESPONSE;
  output[offset++] = response.nodeId;
  put16(output, offset, response.requestId);
  output[offset++] = response.status;
  output[offset++] = response.modbusCode;
  put16(output, offset, response.valueCount);
  for (uint16_t index = 0; index < response.valueCount; ++index) {
    put16(output, offset, response.values[index]);
  }
  if (relayExtension) {
    output[offset++] = response.ttl;
    output[offset++] = response.maxTtl;
  }
  put16(output, offset, crc16(output, offset));
  return offset;
}

inline bool decodeResponse(const uint8_t* input, size_t length,
                           Response& response) {
  if (!verify(input, length, RESPONSE) || length < 12) return false;
  size_t offset = 3;
  response.nodeId = input[offset++];
  response.requestId = get16(input, offset);
  response.status = input[offset++];
  response.modbusCode = input[offset++];
  response.valueCount = get16(input, offset);
  const size_t baseLength = 12U + response.valueCount * 2U;
  const bool relayExtension = length == baseLength + 2U;
  if (response.valueCount > MAX_VALUES ||
      (length != baseLength && !relayExtension)) {
    return false;
  }
  for (uint16_t index = 0; index < response.valueCount; ++index) {
    response.values[index] = get16(input, offset);
  }
  response.ttl = relayExtension ? input[offset++] : 0;
  response.maxTtl = relayExtension ? input[offset++] : 0;
  if (response.ttl > response.maxTtl) return false;
  return true;
}

inline size_t encodeControl(MessageType type, uint8_t nodeId,
                            uint16_t requestId, uint8_t* output,
                            size_t capacity) {
  if (capacity < 8 || (type != PING && type != PONG)) return 0;
  size_t offset = 0;
  output[offset++] = MAGIC;
  output[offset++] = VERSION;
  output[offset++] = type;
  output[offset++] = nodeId;
  put16(output, offset, requestId);
  put16(output, offset, crc16(output, offset));
  return offset;
}

inline bool decodeControl(const uint8_t* input, size_t length,
                          MessageType type, uint8_t& nodeId,
                          uint16_t& requestId) {
  if (length != 8 || !verify(input, length, type)) return false;
  size_t offset = 3;
  nodeId = input[offset++];
  requestId = get16(input, offset);
  return true;
}

// Extended PONG carries uptime, free SRAM and supply voltage. The decoder
// retains support for all older 8-, 12- and 14-byte packet variants.
inline size_t encodePong(uint8_t nodeId, uint16_t requestId,
                         uint32_t uptimeSeconds, uint16_t freeHeap,
                         uint16_t supplyMillivolts, uint8_t* output,
                         size_t capacity) {
  if (capacity < 16) return 0;
  size_t offset = 0;
  output[offset++] = MAGIC;
  output[offset++] = VERSION;
  output[offset++] = PONG;
  output[offset++] = nodeId;
  put16(output, offset, requestId);
  put32(output, offset, uptimeSeconds);
  put16(output, offset, freeHeap);
  put16(output, offset, supplyMillivolts);
  put16(output, offset, crc16(output, offset));
  return offset;
}

inline bool decodePong(const uint8_t* input, size_t length, uint8_t& nodeId,
                       uint16_t& requestId, uint32_t& uptimeSeconds,
                       bool& hasUptime, uint16_t& freeHeap,
                       bool& hasFreeHeap, uint16_t& supplyMillivolts,
                       bool& hasSupplyMillivolts) {
  if ((length != 8 && length != 12 && length != 14 && length != 16) ||
      !verify(input, length, PONG)) {
    return false;
  }
  size_t offset = 3;
  nodeId = input[offset++];
  requestId = get16(input, offset);
  hasUptime = length >= 12;
  uptimeSeconds = hasUptime ? get32(input, offset) : 0;
  hasFreeHeap = length >= 14;
  freeHeap = hasFreeHeap ? get16(input, offset) : 0;
  hasSupplyMillivolts = length == 16;
  supplyMillivolts = hasSupplyMillivolts ? get16(input, offset) : 0;
  return true;
}

inline size_t encodeTopologyDiscovery(const TopologyDiscovery& discovery,
                                      uint8_t* output, size_t capacity) {
  const size_t length = 10U + discovery.pathLength;
  if (discovery.pathLength > MAX_ROUTE_HOPS || length > capacity) return 0;
  size_t offset = 0;
  output[offset++] = MAGIC;
  output[offset++] = VERSION;
  output[offset++] = TOPOLOGY_DISCOVERY;
  put16(output, offset, discovery.requestId);
  output[offset++] = discovery.ttl;
  output[offset++] = discovery.maxTtl;
  output[offset++] = discovery.pathLength;
  for (uint8_t index = 0; index < discovery.pathLength; ++index) {
    output[offset++] = discovery.path[index];
  }
  put16(output, offset, crc16(output, offset));
  return offset;
}

inline bool decodeTopologyDiscovery(const uint8_t* input, size_t length,
                                    TopologyDiscovery& discovery) {
  if (length < 10 || !verify(input, length, TOPOLOGY_DISCOVERY)) return false;
  size_t offset = 3;
  discovery.requestId = get16(input, offset);
  discovery.ttl = input[offset++];
  discovery.maxTtl = input[offset++];
  discovery.pathLength = input[offset++];
  if (discovery.maxTtl > MAX_ROUTE_HOPS ||
      discovery.ttl > discovery.maxTtl ||
      discovery.pathLength > MAX_ROUTE_HOPS ||
      length != 10U + discovery.pathLength) return false;
  for (uint8_t index = 0; index < discovery.pathLength; ++index) {
    discovery.path[index] = input[offset++];
  }
  return true;
}

inline size_t encodeTopologyReport(const TopologyReport& report,
                                   uint8_t* output, size_t capacity) {
  const size_t length = 15U + report.pathLength +
                        (report.hasDiagnostics ? 4U : 0U) +
                        (report.hasTemperature ? 2U : 0U) +
                        (report.hasUid ? DEVICE_UID_SIZE : 0U);
  if (report.pathLength > MAX_ROUTE_HOPS + 1 || length > capacity) return 0;
  size_t offset = 0;
  output[offset++] = MAGIC;
  output[offset++] = VERSION;
  output[offset++] = TOPOLOGY_REPORT;
  output[offset++] = report.nodeId;
  put16(output, offset, report.requestId);
  put32(output, offset, report.uptimeSeconds);
  output[offset++] = report.ttl;
  output[offset++] = report.maxTtl;
  output[offset++] = report.pathLength;
  for (uint8_t index = 0; index < report.pathLength; ++index) {
    output[offset++] = report.path[index];
  }
  if (report.hasDiagnostics) {
    put16(output, offset, report.freeHeap);
    put16(output, offset, report.supplyMillivolts);
    if (report.hasTemperature) {
      put16(output, offset,
            static_cast<uint16_t>(report.temperatureDeciCelsius));
    }
  }
  if (report.hasUid) {
    for (uint8_t index = 0; index < DEVICE_UID_SIZE; ++index) output[offset++] = report.uid[index];
  }
  put16(output, offset, crc16(output, offset));
  return offset;
}

inline bool decodeTopologyReport(const uint8_t* input, size_t length,
                                 TopologyReport& report) {
  if (length < 15 || !verify(input, length, TOPOLOGY_REPORT)) return false;
  size_t offset = 3;
  report.nodeId = input[offset++];
  report.requestId = get16(input, offset);
  report.uptimeSeconds = get32(input, offset);
  report.ttl = input[offset++];
  report.maxTtl = input[offset++];
  report.pathLength = input[offset++];
  const size_t legacyLength = 15U + report.pathLength;
  const size_t diagnosticsLength = legacyLength + 4U;
  const size_t temperatureLength = diagnosticsLength + 2U;
  const size_t uidLength = temperatureLength + DEVICE_UID_SIZE;
  if (report.nodeId == 0 || report.maxTtl > MAX_ROUTE_HOPS ||
      report.ttl > report.maxTtl ||
      report.pathLength > MAX_ROUTE_HOPS + 1 ||
      (length != legacyLength && length != diagnosticsLength &&
       length != temperatureLength && length != uidLength)) return false;
  for (uint8_t index = 0; index < report.pathLength; ++index) {
    report.path[index] = input[offset++];
  }
  report.hasDiagnostics = length >= diagnosticsLength;
  report.freeHeap = report.hasDiagnostics ? get16(input, offset) : 0;
  report.supplyMillivolts = report.hasDiagnostics ? get16(input, offset) : 0;
  report.hasTemperature = length == temperatureLength || length == uidLength;
  report.temperatureDeciCelsius = report.hasTemperature
                                      ? static_cast<int16_t>(get16(input, offset))
                                      : 0;
  report.hasUid = length == uidLength;
  if (report.hasUid) {
    for (uint8_t index = 0; index < DEVICE_UID_SIZE; ++index) report.uid[index] = input[offset++];
  }
  return true;
}

}  // namespace LoraBinary
