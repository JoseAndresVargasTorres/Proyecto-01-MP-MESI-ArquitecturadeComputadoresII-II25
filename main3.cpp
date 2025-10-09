#include "main_memory.hpp"
#include "memory_adapter.hpp"
#include "cache.hpp"
#include "processing_element.hpp"
#include "interconnect.hpp"
#include <iostream>
#include <vector>
#include <iomanip>

static Instruction LD(int rd, int ra){ return {InstructionType::LOAD,  rd, ra, 0, 0}; }
static Instruction ST(int rs, int ra){ return {InstructionType::STORE, rs, ra, 0, 0}; }

int main() {
  MainMemory mm;
  MainMemoryAdapter memIf(mm);

  // ===== Bus y 4 cachés
  Interconnect bus;
  Cache2Way c0(memIf), c1(memIf), c2(memIf), c3(memIf);
  for (auto* c : {&c0,&c1,&c2,&c3}) { c->setBus(&bus); bus.attach(c); }

  // ===== 4 PEs con sus cachés
  ProcessingElement pe0(0), pe1(1), pe2(2), pe3(3);
  pe0.setCache(&c0); pe1.setCache(&c1); pe2.setCache(&c2); pe3.setCache(&c3);

  // ============================================================
  // PRUEBA 1: Cadena de writers en la MISMA dirección (MESI)
  // ============================================================
  const uint64_t ADDR = 0x0000;
  mm.writeDouble(ADDR, 0.0);

  std::cout << "==== PRUEBA 1: Cadena de writers (PE0->PE1->PE2->PE3) ====\n";

  // Todos apuntan a la misma dirección
  for (auto* pe : {&pe0,&pe1,&pe2,&pe3}) pe->setRegister(0, ADDR);

  // PE0 escribe 11.0
  pe0.setRegisterDouble(1, 11.0);
  pe0.loadProgram({ ST(1,0) });
  pe0.executeNextInstruction();

  // PE1 escribe 22.0 (debe invalidar/recoger ownership; C0 en M hará flush al BusRdX)
  pe1.setRegisterDouble(1, 22.0);
  pe1.loadProgram({ ST(1,0) });
  pe1.executeNextInstruction();

  // PE2 escribe 33.0
  pe2.setRegisterDouble(1, 33.0);
  pe2.loadProgram({ ST(1,0) });
  pe2.executeNextInstruction();

  // PE3 escribe 44.0
  pe3.setRegisterDouble(1, 44.0);
  pe3.loadProgram({ ST(1,0) });
  pe3.executeNextInstruction();

  // Lecturas finales (opcional): todos leen el valor final 44.0
  for (auto* pe : {&pe0,&pe1,&pe2,&pe3}) { pe->loadProgram({ LD(2,0) }); pe->executeNextInstruction(); }

  c0.flushAll(); c1.flushAll(); c2.flushAll(); c3.flushAll();
  double v = mm.readDouble(ADDR);
  std::cout << "Mem[" << std::hex << ADDR << std::dec << "] = " << v << " (esperado 44.0)\n";

  auto s0=c0.getStats(), s1=c1.getStats(), s2=c2.getStats(), s3=c3.getStats();
  auto show=[&](const char* n, const Cache2Way::Stats& s){
    std::cout << n
      << "  hits="<<s.hits<<" miss="<<s.misses<<" fills="<<s.line_fills
      << " wbs="<<s.writebacks<<" memR="<<s.mem_reads<<" memW="<<s.mem_writes
      << " | busRd="<<s.bus_rd<<" busRdX="<<s.bus_rdx<<" busInv="<<s.bus_inv
      << " | snoopI="<<s.snoop_to_I<<" snoopS="<<s.snoop_to_S<<" snoopFlush="<<s.snoop_flush
      << "\n";
  };
  show("C0",s0); show("C1",s1); show("C2",s2); show("C3",s3);

  // ============================================================
  // PRUEBA 2: Conflicto de set / LRU (en una caché)
  //   Línea = 32B; 8 sets → salto de 256B mantiene el mismo índice.
  //   A=0x0000, B=0x0100, C=0x0200 caen en el mismo set.
  //   Tres STOREs → 3 misses + 3 fills; se fuerza una evicción dirty.
  // ============================================================
  std::cout << "\n==== PRUEBA 2: Conflicto de set / LRU (en C0) ====\n";
  c0.invalidateAll(); c0.resetStats(); // empezamos con C0 vacía
  // (Usamos PE0 y C0)
  pe0.setRegister(0, 0x0000);  // A
  pe0.setRegisterDouble(1, 10.0);
  pe0.loadProgram({ ST(1,0) }); pe0.executeNextInstruction(); // STORE A: miss -> fill -> M (dirty)

  pe0.setRegister(0, 0x0100);  // B (mismo set que A)
  pe0.setRegisterDouble(1, 20.0);
  pe0.loadProgram({ ST(1,0) }); pe0.executeNextInstruction(); // STORE B: miss -> fill -> M (dirty)

  pe0.setRegister(0, 0x0200);  // C (mismo set, fuerza evicción LRU)
  pe0.setRegisterDouble(1, 30.0);
  pe0.loadProgram({ ST(1,0) }); pe0.executeNextInstruction(); // miss -> evict(A) -> writeback(A) -> fill(C) -> M

  c0.flushAll(); // writeback(B) y writeback(C)

  auto slru = c0.getStats();
  std::cout << "C0 LRU  hits="<<slru.hits<<" miss="<<slru.misses
            << " fills="<<slru.line_fills<<" wbs="<<slru.writebacks
            << " memR="<<slru.mem_reads<<" memW="<<slru.mem_writes << "\n";

  std::cout << "Mem[A=0x0000]="<< mm.readDouble(0x0000)
            << "  Mem[B=0x0100]="<< mm.readDouble(0x0100)
            << "  Mem[C=0x0200]="<< mm.readDouble(0x0200) << "\n";

  return 0;
}
