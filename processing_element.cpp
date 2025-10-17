#include "processing_element.hpp"
#include <cstring>
#include <stdexcept>
#include "cache.hpp"
#include <iostream>

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
            if (!cache_) throw std::runtime_error("PE sin cache (LOAD)");

            uint64_t addr = getRegister(inst.reg_src1);
            double value = 0.0;

            std::cout << "[PE" << pe_id << "] Ejecutando LOAD desde " 
                    << std::hex << addr << std::dec << "\n";

            bool hit = cache_->loadDouble(addr, value);

            std::cout << "[PE" << pe_id << "] LOAD " 
                    << (hit ? "HIT" : "MISS") 
                    << " valor=" << value << "\n";

            setRegisterDouble(inst.reg_dest, value);
            read_ops++;
            pc++;
            break;
        }

        case InstructionType::STORE: {
            if (!cache_) throw std::runtime_error("PE sin cache (STORE)");

            uint64_t addr = getRegister(inst.reg_src1);
            double value = getRegisterDouble(inst.reg_dest);

            std::cout << "[PE" << pe_id << "] Ejecutando STORE en " 
                    << std::hex << addr << std::dec 
                    << " valor=" << value << "\n";

            bool hit = cache_->storeDouble(addr, value);

            std::cout << "[PE" << pe_id << "] STORE " 
                    << (hit ? "HIT" : "MISS") << "\n";

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
            // INC REG - incrementa en 8 bytes (tamaño de double)
            // Nota: Adaptado para iterar sobre direcciones de memoria
            // según el código de la Figura 2 que usa "initial address"
            registers[inst.reg_dest] += 8;
            pc++;
            break;
        }
        
        case InstructionType::DEC: {
            // DEC REG - decrementa en 1 (para contadores)
            registers[inst.reg_dest]--;
            pc++;
            break;
        }
        
        case InstructionType::JNZ: {
            // JNZ label (salta si REG != 0)
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