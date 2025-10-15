#pragma once
#include <vector>
#include <mutex>
#include <cstdint>

enum class BusMsg { BusRd, BusRdX, Invalidate, Flush };

class IBusClient {
public:
  virtual ~IBusClient() = default;
  virtual void snoop(BusMsg msg, uint64_t base_addr) = 0;
};

class Interconnect {
public:
  void attach(IBusClient* c) {
    std::scoped_lock lk(mx_);
    clients_.push_back(c);
  }
  
  void broadcast(IBusClient* src, BusMsg msg, uint64_t base_addr) {
    // Copiar lista de clientes bajo lock
    std::vector<IBusClient*> targets;
    {
      std::scoped_lock lk(mx_);
      targets = clients_;
    }
    
    // Hacer broadcast SIN el mutex del bus
    // Cada caché manejará su propio mutex internamente
    for (auto* c : targets) {
      if (c != src) {
        c->snoop(msg, base_addr);
      }
    }
  }
  
private:
  std::mutex mx_;
  std::vector<IBusClient*> clients_;
};