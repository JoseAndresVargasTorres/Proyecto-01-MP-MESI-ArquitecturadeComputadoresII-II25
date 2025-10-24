#include "processing_element.hpp"
#include "cache.hpp"  // AQUÍ SÍ incluimos cache.hpp porque necesitamos la definición completa
#include <cstring>
#include <stdexcept>

ProcessingElement::ProcessingElement(int id) 
    : pe_id(id), pc(0), read_ops(0), write_ops(0) {
    for (int i = 0; i < 8; i++) {
        registers[i] = 0;
    }
}

void ProcessingElement::loadProgram(const std::vector<Instruction>& prog) {
    program = prog;
    pc = 0;
}

void ProcessingElement::executeNextInstruction() {
    if (pc >= program.size()) {
        return;
    }
    
    Instruction& inst = program[pc];
    
    switch (inst.type) {
        case InstructionType::LOAD: {
            if (!cache_) throw std::runtime_error("PE sin cache (LOAD)");

            uint64_t addr = getRegister(inst.reg_src1);
            double value = 0.0;

            bool hit = cache_->loadDouble(addr, value);
            setRegisterDouble(inst.reg_dest, value);
            read_ops++;
            pc++;
            break;
        }

        case InstructionType::STORE: {
            if (!cache_) throw std::runtime_error("PE sin cache (STORE)");

            uint64_t addr = getRegister(inst.reg_src1);
            double value = getRegisterDouble(inst.reg_dest);

            bool hit = cache_->storeDouble(addr, value);
            write_ops++;
            pc++;
            break;
        }

        case InstructionType::FMUL: {
            double a = getRegisterDouble(inst.reg_src1);
            double b = getRegisterDouble(inst.reg_src2);
            setRegisterDouble(inst.reg_dest, a * b);
            pc++;
            break;
        }
        
        case InstructionType::FADD: {
            double a = getRegisterDouble(inst.reg_src1);
            double b = getRegisterDouble(inst.reg_src2);
            setRegisterDouble(inst.reg_dest, a + b);
            pc++;
            break;
        }
        
        case InstructionType::INC: {
            registers[inst.reg_dest]+= 8;
            pc++;
            break;
        }
        
        case InstructionType::DEC: {
            registers[inst.reg_dest]--;
            pc++;
            break;
        }
        
        case InstructionType::JNZ: {
            if (registers[inst.reg_dest] != 0) {
                pc = inst.label;
            } else {
                pc++;
            }
            break;
        }
    }
}

bool ProcessingElement::hasFinished() const {
    return pc >= program.size();
}

void ProcessingElement::reset() {
    pc = 0;
    for (int i = 0; i < 8; i++) {
        registers[i] = 0;
    }
    resetStats();
}

void ProcessingElement::hardReset() {
    reset();
}

void ProcessingElement::setRegister(int reg_num, uint64_t value) {
    if (reg_num < 0 || reg_num >= 8) {
        throw std::out_of_range("Invalid register number");
    }
    registers[reg_num] = value;
}

uint64_t ProcessingElement::getRegister(int reg_num) const {
    if (reg_num < 0 || reg_num >= 8) {
        throw std::out_of_range("Invalid register number");
    }
    return registers[reg_num];
}

void ProcessingElement::setRegisterDouble(int reg_num, double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(double));
    setRegister(reg_num, bits);
}

double ProcessingElement::getRegisterDouble(int reg_num) const {
    uint64_t bits = getRegister(reg_num);
    double value;
    std::memcpy(&value, &bits, sizeof(double));
    return value;
}

void ProcessingElement::resetStats() {
    read_ops = 0;
    write_ops = 0;
}

// Implementación del método para obtener estado MESI
std::optional<int> ProcessingElement::getMESIStateAsInt(uint64_t addr) const {
    if (cache_) {
        auto mesi = cache_->getLineMESI(addr);
        if (mesi) {
            // Convertir enum MESI a int: I=0, S=1, E=2, M=3
            return static_cast<int>(*mesi);
        }
    }
    return std::nullopt;
}