#ifndef PROCESSING_ELEMENT_HPP
#define PROCESSING_ELEMENT_HPP

#include <cstdint>
#include <vector>
#include <string>

// Forward declaration para evitar dependencia circular
class Cache2Way;

// Tipos de instrucción según ISA especificado
enum class InstructionType {
    LOAD,   // LOAD REG, [REG_addr]
    STORE,  // STORE REG, [REG_addr]
    FMUL,   // FMUL REGd, Ra, Rb
    FADD,   // FADD REGd, Ra, Rb
    INC,    // INC REG (incrementa en 8 bytes para direcciones)
    DEC,    // DEC REG (decrementa en 1 para contadores)
    JNZ     // JNZ label
};

// Estructura de instrucción
struct Instruction {
    InstructionType type;
    int reg_dest;    // Registro destino
    int reg_src1;    // Registro fuente 1
    int reg_src2;    // Registro fuente 2
    int label;       // Para JNZ (índice de instrucción)
};

class ProcessingElement {
private:
    Cache2Way* cache_ = nullptr;  // Puntero a la caché
    int pe_id;
    uint64_t registers[8];  // 8 registros de 64 bits (REG0-REG7)
    std::vector<Instruction> program;  // Programa cargado
    int pc;  // Program counter
    
    // Estadísticas
    uint64_t read_ops;
    uint64_t write_ops;

public:
    ProcessingElement(int id);
    
    // Carga de programa
    void loadProgram(const std::vector<Instruction>& prog);
    
    // Ejecución
    void executeNextInstruction();
    bool hasFinished() const;
    void reset();
    
    // Acceso a registros
    void setRegister(int reg_num, uint64_t value);
    uint64_t getRegister(int reg_num) const;
    
    void setRegisterDouble(int reg_num, double value);
    double getRegisterDouble(int reg_num) const;

    // Estadísticas
    uint64_t getReadOps() const { return read_ops; }
    uint64_t getWriteOps() const { return write_ops; }
    void resetStats();

    // Configuración de caché
    void setCache(Cache2Way* c) { cache_ = c; }
    
    int getPEId() const { return pe_id; }
};

#endif // PROCESSING_ELEMENT_HPP