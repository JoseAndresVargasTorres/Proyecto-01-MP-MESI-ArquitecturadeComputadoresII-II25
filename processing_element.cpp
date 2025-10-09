#include "processing_element.hpp"
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
        return;  // Programa terminado
    }
    
    Instruction& inst = program[pc];
    
    switch (inst.type) {
        case InstructionType::LOAD: {
            // LOAD REG, [REG_addr]
            // Esta instrucción necesita interactuar con caché
            // Por ahora solo incrementa PC
            read_ops++;
            pc++;
            break;
        }
        
        case InstructionType::STORE: {
            // STORE REG, [REG_addr]
            // Esta instrucción necesita interactuar con caché
            write_ops++;
            pc++;
            break;
        }
        
        case InstructionType::FMUL: {
            // FMUL REGd, Ra, Rb
            double a = getRegisterDouble(inst.reg_src1);
            double b = getRegisterDouble(inst.reg_src2);
            setRegisterDouble(inst.reg_dest, a * b);
            pc++;
            break;
        }
        
        case InstructionType::FADD: {
            // FADD REGd, Ra, Rb
            double a = getRegisterDouble(inst.reg_src1);
            double b = getRegisterDouble(inst.reg_src2);
            setRegisterDouble(inst.reg_dest, a + b);
            pc++;
            break;
        }
        
        case InstructionType::INC: {
            // INC REG
            registers[inst.reg_dest]++;
            pc++;
            break;
        }
        
        case InstructionType::DEC: {
            // DEC REG
            registers[inst.reg_dest]--;
            pc++;
            break;
        }
        
        case InstructionType::JNZ: {
            // JNZ label (salta si REG3 != 0, por convención)
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