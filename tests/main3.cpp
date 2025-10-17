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

static const char* mesiName(Cache2Way::MESI m){
  switch(m){ case Cache2Way::MESI::M: return "M";
             case Cache2Way::MESI::E: return "E";
             case Cache2Way::MESI::S: return "S";
             default: return "I"; }
}

int main() {
  MainMemory mm;
  MainMemoryAdapter memIf(mm);

  // ===== Bus y 4 cachés
  Interconnect bus;
  Cache2Way c0(memIf), c1(memIf), c2(memIf), c3(memIf);
  c0.setId(0); c1.setId(1); c2.setId(2); c3.setId(3);     // <- etiquetas para logs
  for (auto* c : {&c0,&c1,&c2,&c3}) { c->setBus(&bus); bus.attach(c); }

  // ===== 4 PEs con sus cachés
  ProcessingElement pe0(0), pe1(1), pe2(2), pe3(3);
  pe0.setCache(&c0); pe1.setCache(&c1); pe2.setCache(&c2); pe3.setCache(&c3);

  // ============================================================
  // PRUEBA 1: Cadena de writers
  // ============================================================
  const uint64_t ADDR = 0x0000;
  mm.writeDouble(ADDR, 0.0);

  std::cout << "==== PRUEBA 1: Cadena de writers (PE0->PE1->PE2->PE3) ====\n";
  for (auto* pe : {&pe0,&pe1,&pe2,&pe3}) pe->setRegister(0, ADDR);

  pe0.setRegisterDouble(1, 11.0); pe0.loadProgram({ ST(1,0) }); pe0.executeNextInstruction();
  pe1.setRegisterDouble(1, 22.0); pe1.loadProgram({ ST(1,0) }); pe1.executeNextInstruction();
  pe2.setRegisterDouble(1, 33.0); pe2.loadProgram({ ST(1,0) }); pe2.executeNextInstruction();
  pe3.setRegisterDouble(1, 44.0); pe3.loadProgram({ ST(1,0) }); pe3.executeNextInstruction();

  for (auto* pe : {&pe0,&pe1,&pe2,&pe3}) { pe->loadProgram({ LD(2,0) }); pe->executeNextInstruction(); }

  c0.flushAll(); c1.flushAll(); c2.flushAll(); c3.flushAll();
  double v = mm.readDouble(ADDR);
  std::cout << "Mem[0x" << std::hex << ADDR << std::dec << "] = " << v << " (esperado 44.0)\n";

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

  // --- Resumen/tabla de estados MESI para la línea 0x0000
  auto m0=c0.getLineMESI(ADDR), m1=c1.getLineMESI(ADDR), m2=c2.getLineMESI(ADDR), m3=c3.getLineMESI(ADDR);
  std::cout << "\n=== Estado MESI final por caché para 0x0000 ===\n";
  std::cout << "C0=" << (m0?mesiName(*m0):"I")
            << "  C1=" << (m1?mesiName(*m1):"I")
            << "  C2=" << (m2?mesiName(*m2):"I")
            << "  C3=" << (m3?mesiName(*m3):"I") << "\n";

  // ============================================================
  // PRUEBA 2: Conflicto de set / LRU en C0
  // ============================================================
  std::cout << "\n==== PRUEBA 2: Conflicto de set / LRU (en C0) ====\n";
  c0.invalidateAll(); c0.resetStats();
  pe0.setRegister(0, 0x0000);  pe0.setRegisterDouble(1, 10.0);
  pe0.loadProgram({ ST(1,0) }); pe0.executeNextInstruction();

  pe0.setRegister(0, 0x0100);  pe0.setRegisterDouble(1, 20.0);
  pe0.loadProgram({ ST(1,0) }); pe0.executeNextInstruction();

  pe0.setRegister(0, 0x0200);  pe0.setRegisterDouble(1, 30.0);
  pe0.loadProgram({ ST(1,0) }); pe0.executeNextInstruction();

  c0.flushAll();

  auto slru = c0.getStats();
  std::cout << "C0 LRU  hits="<<slru.hits<<" miss="<<slru.misses
            << " fills="<<slru.line_fills<<" wbs="<<slru.writebacks
            << " memR="<<slru.mem_reads<<" memW="<<slru.mem_writes << "\n";

  std::cout << "Mem[A=0x0000]="<< mm.readDouble(0x0000)
            << "  Mem[B=0x0100]="<< mm.readDouble(0x0100)
            << "  Mem[C=0x0200]="<< mm.readDouble(0x0200) << "\n";

  return 0;
}

