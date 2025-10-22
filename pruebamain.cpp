#include "main_memory.hpp"
#include "memory_adapter.hpp"
#include "cache.hpp"
#include "processing_element.hpp"
#include "interconnect.hpp"

#include <iostream>
#include <thread>
#include <vector>
#include <iomanip>
#include <cmath>
#include <cstdint>
#include <string>

// ==========================
// Programa de la Figura 2
// ==========================
std::vector<Instruction> crearProgramaProductoPunto() {
    std::vector<Instruction> code;

    // Convenciones:
    // REG0: dirección base de A para este PE
    // REG1: dirección base de B para este PE
    // REG2: dirección donde guardar partial_sums[ID]
    // REG3: contador de iteraciones (N/4)
    // REG4: acumulador (partial_sums[ID])
    // REG5: A[i] temporal
    // REG6: B[i] temporal
    // REG7: A[i]*B[i] temporal

    // Línea 7: LOAD REG4, [REG2]  ; Carga acumulador inicial (0.0)
    code.push_back({InstructionType::LOAD, 4, 2, 0, 0});

    // Línea 8: LOOP:
    int loop_start = (int)code.size();

    // Línea 9: LOAD REG5, [REG0]  ; Carga A[i]
    code.push_back({InstructionType::LOAD, 5, 0, 0, 0});

    // Línea 10: LOAD REG6, [REG1]  ; Carga B[i]
    code.push_back({InstructionType::LOAD, 6, 1, 0, 0});

    // Línea 11: FMUL REG7, REG5, REG6  ; REG7 = A[i] * B[i]
    code.push_back({InstructionType::FMUL, 7, 5, 6, 0});

    // Línea 12: FADD REG4, REG4, REG7  ; REG4 += REG7
    code.push_back({InstructionType::FADD, 4, 4, 7, 0});

    // Línea 13: INC REG0  ; Siguiente elemento de A
    code.push_back({InstructionType::INC, 0, 0, 0, 0});

    // Línea 14: INC REG1  ; Siguiente elemento de B
    code.push_back({InstructionType::INC, 1, 0, 0, 0});

    // Línea 15: DEC REG3  ; Decrementa contador
    code.push_back({InstructionType::DEC, 3, 0, 0, 0});

    // Línea 16: JNZ LOOP  ; Si contador != 0, vuelve a LOOP
    code.push_back({InstructionType::JNZ, 3, 0, 0, loop_start});

    // Línea 17: STORE REG4, [REG2]  ; Guarda resultado parcial
    code.push_back({InstructionType::STORE, 4, 2, 0, 0});

    return code;
}

// ==========================
// Runner por hilo (modo normal)
// ==========================
void ejecutarPE(ProcessingElement* pe, int id) {
    std::cout << "[THREAD PE" << id << "] Iniciando...\n";
    while (!pe->hasFinished()) {
        pe->executeNextInstruction();
    }
    std::cout << "[THREAD PE" << id << "] Terminado.\n";
}

// ==========================
// Helpers de Stepping (solo desde main)
// ==========================
// Reemplaza dump_partial_sums por esto:
static void dump_state(
    MainMemory& memoria, uint64_t base, int npe,
    ProcessingElement& pe0, ProcessingElement& pe1,
    ProcessingElement& pe2, ProcessingElement& pe3,
    bool leer_memoria=true  // pon en false si quieres evitar más lecturas
){
    std::cout << "[estado]\n";
    std::cout << "  R4 (acumulador por PE):\n";
    std::cout << "    PE0.R4 = " << pe0.getRegisterDouble(4) << "\n";
    std::cout << "    PE1.R4 = " << pe1.getRegisterDouble(4) << "\n";
    std::cout << "    PE2.R4 = " << pe2.getRegisterDouble(4) << "\n";
    std::cout << "    PE3.R4 = " << pe3.getRegisterDouble(4) << "\n";

    if (leer_memoria) {
        std::cout << "  partial_sums (memoria):\n";
        for (int i=0; i<npe; ++i) {
            double v = memoria.readDouble(base + i*32);
            std::cout << "    ps[" << i << "] = " << v << "\n";
        }
    }
}


static bool prompt_step(bool &stepping_enabled) {
    std::cout << "\n(step) Enter=next | c=continue | q=quit > ";
    std::string line;
    if (!std::getline(std::cin, line)) {
        // Si stdin se cierra, continuar sin pausas
        stepping_enabled = false;
        return true;
    }
    if (line.empty()) return true;                  // next
    if (line == "c" || line == "C") {               // continuar hasta el final
        stepping_enabled = false;
        return true;
    }
    if (line == "q" || line == "Q") { std::exit(0); } // salir
    return true; // cualquier otra cosa = next
}

// ==========================
// MAIN
// ==========================
int main() {
    // === Config de stepping (puedes cambiarlo rápido aquí) ===
    bool stepping_enabled = true;   // true = modo paso a paso / false = hilos
    int  breakpoint_step  = 5;      // pausa cada K pasos globales
    int  step_count       = 0;      // contador global de pasos

    std::cout << "=== SIMULADOR DE PRODUCTO PUNTO PARALELO ===\n\n";

    // ===== PASO 1: Setup del sistema =====
    std::cout << "1. Inicializando sistema MP...\n";

    MainMemory memoria;
    MainMemoryAdapter adapter(memoria);
    Interconnect bus;

    // Crear 4 cachés
    Cache2Way cache0(adapter), cache1(adapter),
              cache2(adapter), cache3(adapter);

    cache0.setId(0); cache1.setId(1);
    cache2.setId(2); cache3.setId(3);

    // Conectar cachés al bus
    for (auto* c : {&cache0, &cache1, &cache2, &cache3}) {
        c->setBus(&bus);
        bus.attach(c);
    }

    // Crear 4 PEs
    ProcessingElement pe0(0), pe1(1), pe2(2), pe3(3);
    pe0.setCache(&cache0);
    pe1.setCache(&cache1);
    pe2.setCache(&cache2);
    pe3.setCache(&cache3);

    std::cout << "   - 4 PEs creados\n";
    std::cout << "   - 4 cachés privadas creadas\n";
    std::cout << "   - Interconnect configurado\n\n";

    // ===== PASO 2: Cargar vectores en memoria =====
    std::cout << "2. Cargando vectores A y B en memoria...\n";

    const int N = 16;            // Total de elementos
    const int N_per_PE = N / 4;  // 4 elementos por PE

    // Vector A
    double A[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0,
                  9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0};

    // Vector B
    double B[] = {2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0,
                  2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0};

    // Direcciones de memoria:
    // A[] en 0x0000 - 0x0078
    // B[] en 0x0080 - 0x00F8
    // partial_sums[] en 0x0100, 0x0120, 0x0140, 0x0160 (separados por 32 bytes)
    uint64_t addr_A_base             = 0x0000;
    uint64_t addr_B_base             = 0x0080;
    uint64_t addr_partial_sums_base  = 0x0100;

    // Cargar A[] y B[]
    for (int i = 0; i < N; i++) {
        memoria.writeDouble(addr_A_base + i * 8, A[i]);
        memoria.writeDouble(addr_B_base + i * 8, B[i]);
    }

    // Inicializar partial_sums[] en 0.0 (stride 32 bytes para evitar false sharing)
    for (int i = 0; i < 4; i++) {
        memoria.writeDouble(addr_partial_sums_base + i * 32, 0.0);
    }

    std::cout << "   Vector A: [";
    for (int i = 0; i < N; i++) std::cout << A[i] << (i < N-1 ? ", " : "");
    std::cout << "]\n";

    std::cout << "   Vector B: [";
    for (int i = 0; i < N; i++) std::cout << B[i] << (i < N-1 ? ", " : "");
    std::cout << "]\n";

    std::cout << "   N = " << N << " (cada PE procesa " << N_per_PE << " elementos)\n\n";

    // ===== PASO 3: Configurar registros de cada PE =====
    std::cout << "3. Configurando registros de cada PE...\n";

    // PE0: elementos 0-3
    pe0.setRegister(0, addr_A_base + 0  * 8);                 // REG0 = A[0]
    pe0.setRegister(1, addr_B_base + 0  * 8);                 // REG1 = B[0]
    pe0.setRegister(2, addr_partial_sums_base + 0 * 32);      // REG2 = partial_sums[0]
    pe0.setRegister(3, N_per_PE);                             // REG3 = 4

    // PE1: elementos 4-7
    pe1.setRegister(0, addr_A_base + 4  * 8);
    pe1.setRegister(1, addr_B_base + 4  * 8);
    pe1.setRegister(2, addr_partial_sums_base + 1 * 32);
    pe1.setRegister(3, N_per_PE);

    // PE2: elementos 8-11
    pe2.setRegister(0, addr_A_base + 8  * 8);
    pe2.setRegister(1, addr_B_base + 8  * 8);
    pe2.setRegister(2, addr_partial_sums_base + 2 * 32);
    pe2.setRegister(3, N_per_PE);

    // PE3: elementos 12-15
    pe3.setRegister(0, addr_A_base + 12 * 8);
    pe3.setRegister(1, addr_B_base + 12 * 8);
    pe3.setRegister(2, addr_partial_sums_base + 3 * 32);
    pe3.setRegister(3, N_per_PE);

    std::cout << "   PE0: A[0-3]   B[0-3]   -> partial_sums[0]\n";
    std::cout << "   PE1: A[4-7]   B[4-7]   -> partial_sums[1]\n";
    std::cout << "   PE2: A[8-11]  B[8-11]  -> partial_sums[2]\n";
    std::cout << "   PE3: A[12-15] B[12-15] -> partial_sums[3]\n\n";

    // ===== PASO 4: Cargar programa =====
    std::cout << "4. Cargando programa de producto punto...\n";
    auto programa = crearProgramaProductoPunto();
    pe0.loadProgram(programa);
    pe1.loadProgram(programa);
    pe2.loadProgram(programa);
    pe3.loadProgram(programa);
    std::cout << "   Programa cargado (" << programa.size() << " instrucciones por PE)\n\n";

    // ===== PASO 5: Ejecutar (stepping o paralelo) =====
    std::cout << "5. Ejecutando PEs";
    if (stepping_enabled) std::cout << " en modo STEP (cada " << breakpoint_step << " pasos)...\n\n";
    else                  std::cout << " en paralelo...\n\n";

    const int NPE = 4;

    if (!stepping_enabled) {
        // === MODO PARALELO (hilos) ===
        std::vector<std::thread> threads;
        threads.emplace_back(ejecutarPE, &pe0, 0);
        threads.emplace_back(ejecutarPE, &pe1, 1);
        threads.emplace_back(ejecutarPE, &pe2, 2);
        threads.emplace_back(ejecutarPE, &pe3, 3);
        for (auto& t : threads) t.join();
        std::cout << "\n6. Todos los PEs han terminado.\n";
    } else {
        // === MODO STEP (round-robin 1 instrucción por PE) ===
        bool alive[4] = {true, true, true, true};
        int vivos = 4;

        auto one_step = [&](ProcessingElement& pe, int id) {
            if (!alive[id]) return;
            if (pe.hasFinished()) {
                alive[id] = false;
                --vivos;
                return;
            }
            pe.executeNextInstruction();
            ++step_count;
            std::cout << "[step " << step_count << "] Ejecutó PE" << id << "\n";

            if (stepping_enabled && (step_count % breakpoint_step == 0)) {
                dump_state(memoria, addr_partial_sums_base, NPE, pe0, pe1, pe2, pe3, /*leer_memoria=*/false);
                (void)prompt_step(stepping_enabled); // puede desactivar stepping con 'c'
            }
        };

        while (vivos > 0) {
            one_step(pe0, 0);
            one_step(pe1, 1);
            one_step(pe2, 2);
            one_step(pe3, 3);
        }

        std::cout << "\n6. Todos los PEs han terminado (modo STEP).\n";
    }

    // ===== PASO 6: Flush de todas las cachés =====
    std::cout << "\n7. Haciendo flush de cachés...\n";
    cache0.flushAll();
    cache1.flushAll();
    cache2.flushAll();
    cache3.flushAll();
    std::cout << "   Todas las cachés flushed (datos escritos a memoria).\n";

    // ===== PASO 7: Recolectar resultados parciales =====
    std::cout << "\n8. Recolectando resultados parciales...\n";
    double sum0 = memoria.readDouble(addr_partial_sums_base + 0 * 32);
    double sum1 = memoria.readDouble(addr_partial_sums_base + 1 * 32);
    double sum2 = memoria.readDouble(addr_partial_sums_base + 2 * 32);
    double sum3 = memoria.readDouble(addr_partial_sums_base + 3 * 32);

    std::cout << "   partial_sums[0] (PE0) = " << sum0 << "\n";
    std::cout << "   partial_sums[1] (PE1) = " << sum1 << "\n";
    std::cout << "   partial_sums[2] (PE2) = " << sum2 << "\n";
    std::cout << "   partial_sums[3] (PE3) = " << sum3 << "\n\n";

    double resultado_paralelo = sum0 + sum1 + sum2 + sum3;

    // ===== PASO 8: Validar resultado =====
    std::cout << "9. Validando resultado...\n";
    double resultado_serial = 0.0;
    for (int i = 0; i < N; i++) {
        resultado_serial += A[i] * B[i];
    }

    std::cout << "   Resultado PARALELO: " << std::fixed << std::setprecision(2)
              << resultado_paralelo << "\n";
    std::cout << "   Resultado SERIAL:   " << resultado_serial << "\n";

    bool correcto = (std::abs(resultado_paralelo - resultado_serial) < 1e-6);
    std::cout << "   Verificación: " << (correcto ? "✓ CORRECTO" : "✗ ERROR") << "\n\n";

    // ===== PASO 9: Mostrar estadísticas =====
    std::cout << "10. Estadísticas del sistema:\n\n";
    auto mostrarStats = [](const char* nombre, const Cache2Way::Stats& s) {
        std::cout << "   " << nombre << ":\n";
        std::cout << "      Hits: " << s.hits << "  Misses: " << s.misses << "\n";
        std::cout << "      Line fills: " << s.line_fills
                  << "  Writebacks: " << s.writebacks << "\n";
        std::cout << "      Mem reads: " << s.mem_reads
                  << "  Mem writes: " << s.mem_writes << "\n";
        std::cout << "      Bus - BusRd: " << s.bus_rd
                  << "  BusRdX: " << s.bus_rdx
                  << "  Invalidate: " << s.bus_inv << "\n";
        std::cout << "      Snoop - toI: " << s.snoop_to_I
                  << "  toS: " << s.snoop_to_S
                  << "  Flush: " << s.snoop_flush << "\n\n";
    };

    mostrarStats("Cache PE0", cache0.getStats());
    mostrarStats("Cache PE1", cache1.getStats());
    mostrarStats("Cache PE2", cache2.getStats());
    mostrarStats("Cache PE3", cache3.getStats());

    std::cout << "   Memoria Principal:\n";
    std::cout << "      Total reads: " << memoria.getReadCount() << "\n";
    std::cout << "      Total writes: " << memoria.getWriteCount() << "\n\n";

    std::cout << "=== FIN DE LA SIMULACIÓN ===\n";
    return 0;
}
