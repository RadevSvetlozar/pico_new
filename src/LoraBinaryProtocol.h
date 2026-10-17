#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace LoraBinary {

constexpr uint8_t MAGIC = 0xA7;
constexpr uint8_t VERSION = 2; // UID-only radio network; update both endpoints.
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
  uint8_t path[MAX_ROUTE_HOPS] = {}; // reserved wire bytes, not routing addresses
  uint8_t pathUids[MAX_ROUTE_HOPS][DEVICE_UID_SIZE] = {};
};

struct TopologyReport {
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
  uint8_t path[MAX_ROUTE_HOPS + 1] = {};
  uint8_t pathUids[MAX_ROUTE_HOPS + 1][DEVICE_UID_SIZE] = {};
};

enum DataType : uint8_t {
  RAW = 0,
  FLOAT32 = 1,
  INT16 = 2,
  INT32 = 3,
  UINT32 = 4,
};

// Physical service that must execute the UID-addressed request.
enum Target : uint8_t {
  MODBUS = 0,
  INPUTS = 1,
  OUTPUTS = 2,
  CAN_BUS = 3,
  ANALOG_INPUTS = 4,
};

struct Request {
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
  uint16_t requestId = 0;
  uint8_t status = 0;
  uint8_t modbusCode = 0;
  uint16_t valueCount = 0;
  uint16_t values[MAX_VALUES];
  bool hasUid = false;
  uint8_t uid[DEVICE_UID_SIZE] = {};
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
  if (!request.hasUid) return 0;
  const bool relayExtension = request.maxTtl > 0;
  const size_t length = 21U + request.valueCount * 2U +
                        (relayExtension ? 2U : 0U) +
                        (request.hasUid ? DEVICE_UID_SIZE : 0U);
  if (request.valueCount > MAX_VALUES || length > capacity) return 0;
  size_t offset = 0;
  output[offset++] = MAGIC;
  output[offset++] = VERSION;
  output[offset++] = REQUEST;
  output[offset++] = 0; // reserved
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
  ++offset; // reserved
  request.requestId = get16(input, offset);
  if (length < 27 || input[18] > MAX_VALUES) return false;
  const size_t baseLength = 21U + static_cast<size_t>(input[18]) * 2U;
  const bool relayExtension = length == baseLength + DEVICE_UID_SIZE + 2U;
  if (length != baseLength + DEVICE_UID_SIZE && !relayExtension) return false;
  request.target = static_cast<Target>(input[offset++]);
  if (request.target > ANALOG_INPUTS) return false;
  request.slaveId = input[offset++];
  request.function = input[offset++];
  request.address = get16(input, offset);
  request.quantity = get16(input, offset);
  request.dataType = static_cast<DataType>(input[offset++]);
  request.scale = getFloat(input, offset);
  request.valueCount = input[offset++];
  for (uint8_t index = 0; index < request.valueCount; ++index) {
    request.values[index] = get16(input, offset);
  }
  request.ttl = relayExtension ? input[offset++] : 0;
  request.maxTtl = relayExtension ? input[offset++] : 0;
  request.hasUid=true;
  for(uint8_t index=0;index<DEVICE_UID_SIZE;++index)request.uid[index]=input[offset++];
  if (request.ttl > request.maxTtl) return false;
  return true;
}

inline size_t encodeResponse(const Response& response, uint8_t* output,
                             size_t capacity) {
  if (!response.hasUid) return 0;
  const bool relayExtension = response.maxTtl > 0;
  const size_t length = 12U + DEVICE_UID_SIZE + response.valueCount * 2U +
                        (relayExtension ? 2U : 0U);
  if (response.valueCount > MAX_VALUES || length > capacity) return 0;
  size_t offset = 0;
  output[offset++] = MAGIC;
  output[offset++] = VERSION;
  output[offset++] = RESPONSE;
  output[offset++] = 0; // reserved
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
  for (uint8_t i=0;i<DEVICE_UID_SIZE;++i) output[offset++]=response.uid[i];
  put16(output, offset, crc16(output, offset));
  return offset;
}

inline bool decodeResponse(const uint8_t* input, size_t length,
                           Response& response) {
  if (!verify(input, length, RESPONSE) || length < 12) return false;
  size_t offset = 3;
  ++offset; // reserved
  response.requestId = get16(input, offset);
  response.status = input[offset++];
  response.modbusCode = input[offset++];
  response.valueCount = get16(input, offset);
  const size_t baseLength = 12U + DEVICE_UID_SIZE + response.valueCount * 2U;
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
  response.hasUid=true;
  for(uint8_t i=0;i<DEVICE_UID_SIZE;++i)response.uid[i]=input[offset++];
  if (response.ttl > response.maxTtl) return false;
  return true;
}

inline bool responseMatches(const Request& request, const Response& response) {
  return request.hasUid && response.hasUid &&
      request.requestId == response.requestId && request.maxTtl == response.maxTtl &&
      memcmp(request.uid,response.uid,DEVICE_UID_SIZE)==0;
}

inline size_t encodeTopologyDiscovery(const TopologyDiscovery& discovery,
                                      uint8_t* output, size_t capacity) {
  const size_t length = 10U + discovery.pathLength * (1U + DEVICE_UID_SIZE);
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
  for(uint8_t i=0;i<discovery.pathLength;++i)
    for(uint8_t j=0;j<DEVICE_UID_SIZE;++j)output[offset++]=discovery.pathUids[i][j];
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
      length != 10U + discovery.pathLength * (1U + DEVICE_UID_SIZE)) return false;
  for (uint8_t index = 0; index < discovery.pathLength; ++index) {
    discovery.path[index] = input[offset++];
  }
  for(uint8_t i=0;i<discovery.pathLength;++i)
    for(uint8_t j=0;j<DEVICE_UID_SIZE;++j)discovery.pathUids[i][j]=input[offset++];
  return true;
}

inline size_t encodeTopologyReport(const TopologyReport& report,
                                   uint8_t* output, size_t capacity) {
  if (!report.hasUid || !report.hasDiagnostics || !report.hasTemperature) return 0;
  const size_t length = 15U + report.pathLength * (1U + DEVICE_UID_SIZE) +
                        (report.hasDiagnostics ? 4U : 0U) +
                        (report.hasTemperature ? 2U : 0U) +
                        (report.hasUid ? DEVICE_UID_SIZE : 0U);
  if (report.pathLength > MAX_ROUTE_HOPS + 1 || length > capacity) return 0;
  size_t offset = 0;
  output[offset++] = MAGIC;
  output[offset++] = VERSION;
  output[offset++] = TOPOLOGY_REPORT;
  output[offset++] = 0; // reserved
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
  for(uint8_t i=0;i<report.pathLength;++i)
    for(uint8_t j=0;j<DEVICE_UID_SIZE;++j)output[offset++]=report.pathUids[i][j];
  put16(output, offset, crc16(output, offset));
  return offset;
}

inline bool decodeTopologyReport(const uint8_t* input, size_t length,
                                 TopologyReport& report) {
  if (length < 15 || !verify(input, length, TOPOLOGY_REPORT)) return false;
  size_t offset = 3;
  ++offset; // reserved
  report.requestId = get16(input, offset);
  report.uptimeSeconds = get32(input, offset);
  report.ttl = input[offset++];
  report.maxTtl = input[offset++];
  report.pathLength = input[offset++];
  const size_t legacyLength = 15U + report.pathLength * (1U + DEVICE_UID_SIZE);
  const size_t diagnosticsLength = legacyLength + 4U;
  const size_t temperatureLength = diagnosticsLength + 2U;
  const size_t uidLength = temperatureLength + DEVICE_UID_SIZE;
  if (report.maxTtl > MAX_ROUTE_HOPS ||
      report.ttl > report.maxTtl ||
      report.pathLength > MAX_ROUTE_HOPS + 1 ||
      length != uidLength) return false;
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
  for(uint8_t i=0;i<report.pathLength;++i)
    for(uint8_t j=0;j<DEVICE_UID_SIZE;++j)report.pathUids[i][j]=input[offset++];
  return true;
}

}  // namespace LoraBinary
