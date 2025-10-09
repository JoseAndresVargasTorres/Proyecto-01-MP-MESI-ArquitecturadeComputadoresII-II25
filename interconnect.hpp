#pragma once
#include <vector>
#include <mutex>
#include <cstdint>

enum class BusMsg { BusRd, BusRdX, Invalidate, Flush };

class IBusClient {
public:
  virtual ~IBusClient() = default;
  // base_addr = dirección base de la línea (alineada a 32B)
  virtual void snoop(BusMsg msg, uint64_t base_addr) = 0;
};

class Interconnect {
public:
  void attach(IBusClient* c) {
    std::scoped_lock lk(mx_);
    clients_.push_back(c);
  }
  void broadcast(IBusClient* src, BusMsg msg, uint64_t base_addr) {
    std::scoped_lock lk(mx_);
    for (auto* c : clients_) if (c != src) c->snoop(msg, base_addr);
  }
private:
  std::mutex mx_;
  std::vector<IBusClient*> clients_;
};
