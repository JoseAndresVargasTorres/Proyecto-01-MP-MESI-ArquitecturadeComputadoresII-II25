// memory_adapter.hpp
#pragma once
#include "cache.hpp"
#include "main_memory.hpp"

// Adaptador que expone la interfaz que espera Cache2Way sin tocar tu MainMemory
class MainMemoryAdapter : public IMainMemory {
public:
  explicit MainMemoryAdapter(MainMemory& mem) : mem_(mem) {}
  void read64(uint64_t addr, uint64_t& out) override { out = mem_.readWord(addr); }
  void write64(uint64_t addr, uint64_t value) override { mem_.writeWord(addr, value); }
private:
  MainMemory& mem_;
};
