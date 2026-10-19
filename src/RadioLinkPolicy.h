#pragma once
#include <stdint.h>

namespace RadioLink {
constexpr uint8_t SAMPLE_COUNT = 3;
struct Samples {
  uint8_t attempts = 0, replies = 0;
  uint32_t latencySum = 0;
  int32_t rssiSum = 0;
  float snrSum = 0;
  void observe(bool received, uint32_t latency = 0, int rssi = 0, float snr = 0) {
    ++attempts;
    if (received) { ++replies; latencySum += latency; rssiSum += rssi; snrSum += snr; }
  }
  uint8_t percent() const { return attempts ? replies * 100U / attempts : 0; }
};
struct Profile {
  Samples direct, relay;
  uint8_t preferredTtl = 0, consecutiveMisses = 0;
  bool measured = false, needsRecheck = false;
  bool needsRelayTest() const { return direct.attempts == SAMPLE_COUNT && direct.replies == 0; }
  bool testingComplete() const { return direct.attempts == SAMPLE_COUNT && ((direct.replies > 0 && relay.attempts == 0) || relay.attempts == SAMPLE_COUNT); }
  void select() {
    // Prefer the simpler mode on equal delivery rates. TTL permits relays; it
    // never asserts which exact forward/return path a received packet used.
    preferredTtl = relay.percent() > direct.percent() ? 2 : 0;
    measured = testingComplete();
    consecutiveMisses = 0; needsRecheck = false;
  }
  const char* quality() const {
    const uint8_t best = direct.percent() > relay.percent() ? direct.percent() : relay.percent();
    return !measured ? "unmeasured" : best == 0 ? "unreachable" : best >= 80 ? "reliable" : "unstable";
  }
  bool observeRuntime(bool received) {
    if (received) { consecutiveMisses = 0; return false; }
    if (++consecutiveMisses < 3) return false;
    preferredTtl = preferredTtl ? 0 : 2;
    consecutiveMisses = 0; needsRecheck = true;
    return true;
  }
};
}
