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
#include <algorithm>

// ==========================
// Configuración del Sistema
// ==========================
struct SystemConfig {
    int num_pes;                    // Número de Processing Elements
    int vector_size;                // Tamaño total de los vectores
    uint64_t addr_A_base;           // Dirección base del vector A
    uint64_t addr_B_base;           // Dirección base del vector B
    uint64_t addr_partial_sums_base;// Dirección base de sumas parciales
    int partial_sum_stride;         // Separación entre sumas parciales (evitar false sharing)
    
    SystemConfig(int n_pes = 4, int n = 16) 
        : num_pes(n_pes), 
          vector_size(n),
          addr_A_base(0x0000),
          addr_B_base(0x0080 + (n * 8)),  // Después de A[]
          addr_partial_sums_base(0x0080 + (2 * n * 8)),  // Después de B[]
          partial_sum_stride(32) {}  // 32 bytes para evitar false sharing
};

// ==========================
// Generador de Programa
// ==========================
std::vector<Instruction> crearProgramaProductoPunto() {
    std::vector<Instruction> code;

    // Convenciones de registros:
    // REG0: dirección actual de A para este PE
    // REG1: dirección actual de B para este PE
    // REG2: dirección donde guardar partial_sums[ID]
    // REG3: contador de iteraciones (elementos a procesar)
    // REG4: acumulador (partial_sums[ID])
    // REG5: A[i] temporal
    // REG6: B[i] temporal
    // REG7: A[i]*B[i] temporal

    // Línea 1: LOAD REG4, [REG2]  ; Carga acumulador inicial (0.0)
    code.push_back({InstructionType::LOAD, 4, 2, 0, 0});

    // Línea 2: LOOP:
    int loop_start = (int)code.size();

    // Línea 3: LOAD REG5, [REG0]  ; Carga A[i]
    code.push_back({InstructionType::LOAD, 5, 0, 0, 0});

    // Línea 4: LOAD REG6, [REG1]  ; Carga B[i]
    code.push_back({InstructionType::LOAD, 6, 1, 0, 0});

    // Línea 5: FMUL REG7, REG5, REG6  ; REG7 = A[i] * B[i]
    code.push_back({InstructionType::FMUL, 7, 5, 6, 0});

    // Línea 6: FADD REG4, REG4, REG7  ; REG4 += REG7
    code.push_back({InstructionType::FADD, 4, 4, 7, 0});

    // Línea 7: INC REG0  ; Siguiente elemento de A
    code.push_back({InstructionType::INC, 0, 0, 0, 0});

    // Línea 8: INC REG1  ; Siguiente elemento de B
    code.push_back({InstructionType::INC, 1, 0, 0, 0});

    // Línea 9: DEC REG3  ; Decrementa contador
    code.push_back({InstructionType::DEC, 3, 0, 0, 0});

    // Línea 10: JNZ LOOP  ; Si contador != 0, vuelve a LOOP
    code.push_back({InstructionType::JNZ, 3, 0, 0, loop_start});

    // Línea 11: STORE REG4, [REG2]  ; Guarda resultado parcial
    code.push_back({InstructionType::STORE, 4, 2, 0, 0});

    return code;
}

// ==========================
// Inicialización de Vectores
// ==========================
void inicializarVectores(MainMemory& memoria, const SystemConfig& config,
                         std::vector<double>& A, std::vector<double>& B) {
    std::cout << "2. Inicializando vectores de tamaño " << config.vector_size << "...\n";
    
    // Inicializar vectores A y B
    A.resize(config.vector_size);
    B.resize(config.vector_size);
    
    for (int i = 0; i < config.vector_size; i++) {
        A[i] = static_cast<double>(i + 1);  // A = [1, 2, 3, ..., N]
        B[i] = 2.0;                          // B = [2, 2, 2, ..., 2]
    }
    
    // Cargar vectores en memoria
    for (int i = 0; i < config.vector_size; i++) {
        memoria.writeDouble(config.addr_A_base + i * 8, A[i]);
        memoria.writeDouble(config.addr_B_base + i * 8, B[i]);
    }
    
    // Inicializar partial_sums en 0.0
    for (int i = 0; i < config.num_pes; i++) {
        memoria.writeDouble(config.addr_partial_sums_base + i * config.partial_sum_stride, 0.0);
    }
    
    std::cout << "   Vector A: [";
    for (int i = 0; i < std::min(8, config.vector_size); i++) 
        std::cout << A[i] << (i < std::min(7, config.vector_size - 1) ? ", " : "");
    if (config.vector_size > 8) std::cout << ", ...";
    std::cout << "]\n";
    
    std::cout << "   Vector B: [";
    for (int i = 0; i < std::min(8, config.vector_size); i++) 
        std::cout << B[i] << (i < std::min(7, config.vector_size - 1) ? ", " : "");
    if (config.vector_size > 8) std::cout << ", ...";
    std::cout << "]\n\n";
}

// ==========================
// Configuración de PEs
// ==========================
void configurarPE(ProcessingElement& pe, int pe_id, const SystemConfig& config) {
    // Calcular segmento que procesa este PE
    int elementos_base = config.vector_size / config.num_pes;
    int elementos_extra = config.vector_size % config.num_pes;
    
    // Distribución: Los primeros PEs toman un elemento extra si hay resto
    int inicio, elementos_a_procesar;
    
    if (pe_id < elementos_extra) {
        // Este PE procesa un elemento extra
        elementos_a_procesar = elementos_base + 1;
        inicio = pe_id * elementos_a_procesar;
    } else {
        // Este PE procesa la cantidad base
        elementos_a_procesar = elementos_base;
        inicio = elementos_extra * (elementos_base + 1) + 
                 (pe_id - elementos_extra) * elementos_base;
    }
    
    int fin = inicio + elementos_a_procesar - 1;
    
    // Configurar registros
    pe.setRegister(0, config.addr_A_base + inicio * 8);  // REG0 = &A[inicio]
    pe.setRegister(1, config.addr_B_base + inicio * 8);  // REG1 = &B[inicio]
    pe.setRegister(2, config.addr_partial_sums_base + pe_id * config.partial_sum_stride);  // REG2 = &partial_sums[pe_id]
    pe.setRegister(3, elementos_a_procesar);              // REG3 = contador
    
    std::cout << "   PE" << pe_id << ": procesa elementos [" << inicio << "-" << fin 
              << "] (" << elementos_a_procesar << " elementos)\n";
}

// ==========================
// Helpers de Stepping
// ==========================
static void dump_state(MainMemory& memoria, const SystemConfig& config,
                      const std::vector<ProcessingElement*>& pes,
                      bool leer_memoria = true) {
    std::cout << "[estado]\n";
    std::cout << "  R4 (acumulador por PE):\n";
    for (size_t i = 0; i < pes.size(); i++) {
        std::cout << "    PE" << i << ".R4 = " << pes[i]->getRegisterDouble(4) << "\n";
    }
    
    if (leer_memoria) {
        std::cout << "  partial_sums (memoria):\n";
        for (int i = 0; i < config.num_pes; i++) {
            double v = memoria.readDouble(config.addr_partial_sums_base + i * config.partial_sum_stride);
            std::cout << "    ps[" << i << "] = " << v << "\n";
        }
    }
}

static bool prompt_step(bool &stepping_enabled) {
    std::cout << "\n(step) Enter=next | c=continue | q=quit > ";
    std::string line;
    if (!std::getline(std::cin, line)) {
        stepping_enabled = false;
        return true;
    }
    if (line.empty()) return true;
    if (line == "c" || line == "C") {
        stepping_enabled = false;
        return true;
    }
    if (line == "q" || line == "Q") { std::exit(0); }
    return true;
}

// ==========================
// Runner por hilo
// ==========================
void ejecutarPE(ProcessingElement* pe, int id) {
    std::cout << "[THREAD PE" << id << "] Iniciando...\n";
    while (!pe->hasFinished()) {
        pe->executeNextInstruction();
    }
    std::cout << "[THREAD PE" << id << "] Terminado.\n";
}

// ==========================
// MAIN
// ==========================
int main(int argc, char* argv[]) {
    // === Configuración del sistema ===
    int N = 16;  // Tamaño por defecto
    int NPE = 4; // Número de PEs por defecto
    
    // Permitir configuración por línea de comandos
    if (argc > 1) N = std::atoi(argv[1]);
    if (argc > 2) NPE = std::atoi(argv[2]);
    
    // Validaciones
    if (N <= 0) {
        std::cerr << "Error: N debe ser positivo\n";
        return 1;
    }
    if (NPE <= 0 || NPE > N) {
        std::cerr << "Error: NPE debe ser positivo y <= N\n";
        return 1;
    }
    
    SystemConfig config(NPE, N);
    
    // === Config de stepping ===
    bool stepping_enabled = false;  // Cambiar a true para modo paso a paso
    int breakpoint_step = 10;
    int step_count = 0;
    
    std::cout << "=== SIMULADOR DE PRODUCTO PUNTO PARALELO ===\n";
    std::cout << "Configuración: N=" << N << ", PEs=" << NPE << "\n\n";
    
    // ===== PASO 1: Setup del sistema =====
    std::cout << "1. Inicializando sistema MP...\n";
    
    MainMemory memoria;
    MainMemoryAdapter adapter(memoria);
    Interconnect bus;
    
    // Crear cachés y PEs dinámicamente
    std::vector<Cache2Way*> caches;
    std::vector<ProcessingElement*> pes;
    
    for (int i = 0; i < NPE; i++) {
        Cache2Way* cache = new Cache2Way(adapter);
        cache->setId(i);
        cache->setBus(&bus);
        bus.attach(cache);
        caches.push_back(cache);
        
        ProcessingElement* pe = new ProcessingElement(i);
        pe->setCache(cache);
        pes.push_back(pe);
    }
    
    std::cout << "   - " << NPE << " PEs creados\n";
    std::cout << "   - " << NPE << " cachés privadas creadas\n";
    std::cout << "   - Interconnect configurado\n\n";
    
    // ===== PASO 2: Cargar vectores =====
    std::vector<double> A, B;
    inicializarVectores(memoria, config, A, B);
    
    // ===== PASO 3: Configurar PEs =====
    std::cout << "3. Configurando registros de cada PE...\n";
    for (int i = 0; i < NPE; i++) {
        configurarPE(*pes[i], i, config);
    }
    std::cout << "\n";
    
    // ===== PASO 4: Cargar programa =====
    std::cout << "4. Cargando programa de producto punto...\n";
    auto programa = crearProgramaProductoPunto();
    for (int i = 0; i < NPE; i++) {
        pes[i]->loadProgram(programa);
    }
    std::cout << "   Programa cargado (" << programa.size() << " instrucciones por PE)\n\n";
    
    // ===== PASO 5: Ejecutar =====
    std::cout << "5. Ejecutando PEs";
    if (stepping_enabled) std::cout << " en modo STEP...\n\n";
    else std::cout << " en paralelo...\n\n";
    
    if (!stepping_enabled) {
        // Modo paralelo
        std::vector<std::thread> threads;
        for (int i = 0; i < NPE; i++) {
            threads.emplace_back(ejecutarPE, pes[i], i);
        }
        for (auto& t : threads) t.join();
        std::cout << "\n6. Todos los PEs han terminado.\n";
    } else {
        // Modo step
        std::vector<bool> alive(NPE, true);
        int vivos = NPE;
        
        auto one_step = [&](int id) {
            if (!alive[id]) return;
            if (pes[id]->hasFinished()) {
                alive[id] = false;
                --vivos;
                return;
            }
            pes[id]->executeNextInstruction();
            ++step_count;
            std::cout << "[step " << step_count << "] Ejecutó PE" << id << "\n";
            
            if (stepping_enabled && (step_count % breakpoint_step == 0)) {
                dump_state(memoria, config, pes, false);
                (void)prompt_step(stepping_enabled);
            }
        };
        
        while (vivos > 0) {
            for (int i = 0; i < NPE; i++) {
                one_step(i);
            }
        }
        std::cout << "\n6. Todos los PEs han terminado (modo STEP).\n";
    }
    
    // ===== PASO 6: Flush cachés =====
    std::cout << "\n7. Haciendo flush de cachés...\n";
    for (auto* cache : caches) {
        cache->flushAll();
    }
    std::cout << "   Todas las cachés flushed.\n";
    
    // ===== PASO 7: Recolectar resultados =====
    std::cout << "\n8. Recolectando resultados parciales...\n";
    double resultado_paralelo = 0.0;
    for (int i = 0; i < NPE; i++) {
        double sum = memoria.readDouble(config.addr_partial_sums_base + i * config.partial_sum_stride);
        std::cout << "   partial_sums[" << i << "] (PE" << i << ") = " << sum << "\n";
        resultado_paralelo += sum;
    }
    std::cout << "\n";
    
    // ===== PASO 8: Validar =====
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
    
    // ===== PASO 9: Estadísticas =====
    std::cout << "10. Estadísticas del sistema:\n\n";
    auto mostrarStats = [](int id, const Cache2Way::Stats& s) {
        std::cout << "   Cache PE" << id << ":\n";
        std::cout << "      Hits: " << s.hits << "  Misses: " << s.misses << "\n";
        std::cout << "      Line fills: " << s.line_fills
                  << "  Writebacks: " << s.writebacks << "\n";
        std::cout << "      Bus - BusRd: " << s.bus_rd
                  << "  BusRdX: " << s.bus_rdx << "\n\n";
    };
    
    for (int i = 0; i < NPE; i++) {
        mostrarStats(i, caches[i]->getStats());
    }
    
    std::cout << "   Memoria Principal:\n";
    std::cout << "      Total reads: " << memoria.getReadCount() << "\n";
    std::cout << "      Total writes: " << memoria.getWriteCount() << "\n\n";
    
    // Limpieza
    for (auto* pe : pes) delete pe;
    for (auto* cache : caches) delete cache;
    
    std::cout << "=== FIN DE LA SIMULACIÓN ===\n";
    return 0;
}