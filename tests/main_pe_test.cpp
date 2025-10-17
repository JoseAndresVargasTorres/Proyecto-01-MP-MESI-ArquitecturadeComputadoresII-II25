#include "main_memory.hpp"
#include "memory_adapter.hpp"
#include "cache.hpp"
#include "processing_element.hpp"
#include <vector>
#include <iostream>

int main() {
    MainMemory mm;
    MainMemoryAdapter memIf(mm);
    Cache2Way cache(memIf);

    // (Opcional) deja alguna data en memoria de fondo
    mm.writeDouble(0x0000, 3.14159);

    // --- PRUEBA VÍA PE ---
    cache.resetStats();                 // limpia stats para medir solo el PE

    // Programa: LOAD R1, [R0]; STORE R1, [R0]
    std::vector<Instruction> prog = {
        { InstructionType::LOAD,  /*reg_dest=*/1, /*reg_src1=*/0, /*reg_src2=*/0, /*label=*/0 },
        { InstructionType::STORE, /*reg_dest=*/1, /*reg_src1=*/0, /*reg_src2=*/0, /*label=*/0 }
    };

    ProcessingElement pe0(0);
    pe0.setCache(&cache);

    // Fondo: Mem[0x0000] = 3.14159; R0 = 0x0000
    mm.writeDouble(0x0000, 3.14159);
    pe0.setRegister(0, 0x0000);
    pe0.loadProgram(prog);

    // Ejecuta 2 instrucciones del PE
    pe0.executeNextInstruction();   // LOAD → miss (fill), R1=3.14159
    pe0.executeNextInstruction();   // STORE → hit, queda dirty

    cache.flushAll();               // fuerza write-back

    // Verifica que el registro quedó cargado
    double r1 = pe0.getRegisterDouble(1);
    std::cout << "R1 = " << r1 << "\n";

    // Verifica que la memoria quedó con el valor tras el STORE
    double m0 = mm.readDouble(0x0000);
    std::cout << "Mem[0x0000] = " << m0 << "\n";

    std::cout << "\n== Prueba 2: LRU + write-back por eviccion ==\n";

    
    cache.resetStats();

    cache.invalidateAll();
    

    // Reusamos pe0 y cache. Seteamos:
    // R1 = 10.0 y STORE en A (0x0000)
    // R1 = 20.0 y STORE en B (0x0100)  // mismo set, otro tag
    // R1 = 30.0 y STORE en C (0x0200)  // mismo set, fuerza eviccion (2 ways)

    // Helper: programa de un solo STORE R1, [R0]
    auto prog_store = std::vector<Instruction>{
        { InstructionType::STORE, /*reg_dest=*/1, /*reg_src1=*/0, /*reg_src2=*/0, /*label=*/0 }
    };

    // 1) STORE A (0x0000): miss, queda dirty
    pe0.loadProgram(prog_store);
    pe0.setRegister(0, 0x0000);           // R0 = addr A
    pe0.setRegisterDouble(1, 10.0);       // R1 = valor
    pe0.executeNextInstruction();

    // 2) STORE B (0x0100): miss, queda dirty (mismo set, otro tag)
    pe0.loadProgram(prog_store);
    pe0.setRegister(0, 0x0100);           // R0 = addr B (256B offset → mismo set)
    pe0.setRegisterDouble(1, 20.0);
    pe0.executeNextInstruction();

    // 3) STORE C (0x0200): miss, fuerza evicción LRU (A). Evicción escribe A (write-back),
    //    luego fill de C y queda dirty.
    pe0.loadProgram(prog_store);
    pe0.setRegister(0, 0x0200);           // R0 = addr C (otro 256B → mismo set)
    pe0.setRegisterDouble(1, 30.0);
    pe0.executeNextInstruction();

    // Forzar write-backs finales de lo sucio (B y C)
    cache.flushAll();

    // Stats esperadas: misses=3, hits=0, fills=3, writebacks=3, memR=12, memW=12 (ver explicación)
    auto stLRU = cache.getStats();
    std::cout << "LRU Stats  hits=" << stLRU.hits
            << "  misses=" << stLRU.misses
            << "  fills=" << stLRU.line_fills
            << "  writebacks=" << stLRU.writebacks
            << "  memR=" << stLRU.mem_reads
            << "  memW=" << stLRU.mem_writes << "\n";

    // Sanity: verificar memoria de fondo (después del flush, B y C deben estar escritos)
    double mA = mm.readDouble(0x0000);
    double mB = mm.readDouble(0x0100);
    double mC = mm.readDouble(0x0200);
    std::cout << "Mem[A=0x0000]=" << mA
            << "  Mem[B=0x0100]=" << mB
            << "  Mem[C=0x0200]=" << mC << "\n";
}
