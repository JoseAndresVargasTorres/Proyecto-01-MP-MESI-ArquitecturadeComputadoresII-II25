#include "main_memory.hpp"
#include "memory_adapter.hpp"
#include "cache.hpp"
#include <iostream>
#include <cassert>

int main() {
  MainMemory mm;
  MainMemoryAdapter memIf(mm);
  Cache2Way cache(memIf);

  // Pre-carga en memoria de fondo
  mm.writeDouble(0x0000, 3.14159);

  double d = 0.0; bool hit = false;

  // 1) LOAD: miss -> hit
  hit = cache.loadDouble(0x0000, d);         // debe ser miss
  std::cout << "Load1 hit=" << hit << " d=" << d << "\n";
  hit = cache.loadDouble(0x0000, d);         // ahora hit
  std::cout << "Load2 hit=" << hit << " d=" << d << "\n";

  // 2) STORE: write-allocate (primero miss), luego hit
  hit = cache.storeDouble(0x0020, 2.71828);  // 0x20 = 32B -> otra línea; debe ser miss
  std::cout << "Store1 hit=" << hit << "\n";
  hit = cache.storeDouble(0x0020, 2.71828);  // ahora hit
  std::cout << "Store2 hit=" << hit << "\n";

  // 3) Forzar evicción en MISMO set (salto 256B preserva índice de set)
  cache.storeDouble(0x0000, 10.0);           // ensucia línea A (set X)
  cache.storeDouble(0x0100, 20.0);           // línea B, mismo set X (256 = 8sets*32B)
  cache.storeDouble(0x0200, 30.0);           // línea C, mismo set X -> fuerza evicción (LRU)

  cache.flushAll(); // write-back de sucio pendiente

  auto st = cache.getStats();
  std::cout << "Stats: hits=" << st.hits
            << " misses=" << st.misses
            << " fills=" << st.line_fills
            << " writebacks=" << st.writebacks
            << " memR=" << st.mem_reads
            << " memW=" << st.mem_writes
            << "\n";
}
