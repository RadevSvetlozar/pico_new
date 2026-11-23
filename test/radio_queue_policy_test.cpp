#include "../src/RadioQueuePolicy.h"
#include <assert.h>
#include <deque>
#include <stdint.h>
struct Request { uint8_t priority; bool pollingRequest; int id; };
struct Client {
  int pending = 40, loops = 0;
  bool connected() { return true; }
  bool loop() { ++loops; if (pending) --pending; return true; }
};
struct BacklogClient {
  std::deque<Request> queue;
  int received = 0;
  bool connected() { return true; }
  bool loop() {
    if (received > 40) return true;
    Request incoming = {static_cast<uint8_t>(received == 40 ? 0 : 1), received != 40, received};
    ++received;
    if (!RadioQueuePolicy::makeRoom(queue,20,incoming.priority)) return true;
    auto position = queue.begin();
    while (position != queue.end() && position->priority <= incoming.priority) ++position;
    queue.insert(position,incoming);
    return true;
  }
};
int main() {
  using namespace RadioQueuePolicy;
  assert(priority(6, true, 2) == 0);
  assert(priority(16, false, 2) == 0);
  assert(priority(3, true, 2) == 0);
  assert(priority(3, false, -1) == 1);
  assert(priority(3, false, 0) == 1);
  assert(priority(4, false, 2) == 2);
  std::deque<Request> queue;
  for(int i=0;i<20;++i)queue.push_back({1,true,i});
  assert(!makeRoom(queue,20,1));assert(queue.size()==20);
  assert(makeRoom(queue,20,0));assert(queue.size()==19);
  queue.push_front({0,false,99});
  assert(queue.front().id==99);assert(queue.size()==20);
  assert(makeRoom(queue,20,0));assert(queue.front().id==99);
  queue.clear();for(int i=0;i<20;++i)queue.push_back({0,false,i});
  assert(!makeRoom(queue,20,0));assert(queue.size()==20);
  queue.back()={2,false,25};assert(!makeRoom(queue,20,0)); // preserve explicit tests
  Client client;uint32_t time=0;
  pumpMqtt(client,[&](){return time;});
  assert(client.pending==0&&client.loops==64); // command behind 40 polls reaches callback
  BacklogClient backlog;
  pumpMqtt(backlog,[]() -> uint32_t {return 0;});
  assert(backlog.received==41 && backlog.queue.size()==20);
  assert(backlog.queue.front().id==40 && !backlog.queue.front().pollingRequest);
  client.loops=0;time=UINT32_MAX-10;
  pumpMqtt(client,[&](){return time++;});assert(client.loops==25); // wrap-safe budget
}
