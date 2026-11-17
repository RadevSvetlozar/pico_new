#pragma once
#include <stdint.h>
#include <stddef.h>

namespace RadioQueuePolicy {
inline bool isWrite(int function) {
  return function == 5 || function == 6 || function == 15 || function == 16;
}
inline uint8_t priority(int function, bool command, int requested) {
  if (isWrite(function) || command) return 0;
  return requested < 0 ? 1 : static_cast<uint8_t>(requested > 2 ? 2 : requested);
}
// Displace only a periodic sample, never an accepted command or test request.
template<class Queue>
bool makeRoom(Queue& queue, size_t limit, uint8_t incomingPriority) {
  if (queue.size() < limit) return true;
  if (incomingPriority != 0) return false;
  auto victim = queue.end();
  for (auto it = queue.begin(); it != queue.end(); ++it) {
    if (it->pollingRequest && it->priority > incomingPriority &&
        (victim == queue.end() || it->priority >= victim->priority)) victim = it;
  }
  if (victim == queue.end()) return false;
  queue.erase(victim);
  return true;
}
// PubSubClient processes one inbound packet per loop(). Drain a bounded batch
// before a slow radio transaction so a command can reach the priority queue.
template<class Client, class Clock>
void pumpMqtt(Client& client, Clock now) {
  const uint32_t started = now();
  for (uint8_t count = 0; count < 64 && client.connected(); ++count) {
    if (!client.loop() || static_cast<uint32_t>(now() - started) >= 25) break;
  }
}
} // namespace RadioQueuePolicy
