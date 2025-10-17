#include "main_memory.hpp"
#include <cstring>

MainMemory::MainMemory() : read_count(0), write_count(0) {
    memory.resize(MEM_SIZE_WORDS, 0);
}

void MainMemory::checkAlignment(uint64_t addr) const {
    if (addr % 8 != 0) {
        throw std::runtime_error("Unaligned memory access");
    }
}

void MainMemory::checkBounds(uint64_t addr) const {
    if (addr / 8 >= MEM_SIZE_WORDS) {
        throw std::out_of_range("Memory address out of range");
    }
}

void MainMemory::writeWord(uint64_t addr, uint64_t data) {
    checkAlignment(addr);
    checkBounds(addr);
    
    std::lock_guard<std::mutex> lock(mem_mutex);
    memory[addr / 8] = data;
    write_count++;
}

uint64_t MainMemory::readWord(uint64_t addr) const {
    checkAlignment(addr);
    checkBounds(addr);
    
    std::lock_guard<std::mutex> lock(mem_mutex);
    read_count++;
    return memory[addr / 8];
}

void MainMemory::writeDouble(uint64_t addr, double data) {
    uint64_t bits;
    std::memcpy(&bits, &data, sizeof(double));
    writeWord(addr, bits);
}

double MainMemory::readDouble(uint64_t addr) const {
    uint64_t bits = readWord(addr);
    double data;
    std::memcpy(&data, &bits, sizeof(double));
    return data;
}

void MainMemory::resetStats() {
    read_count = 0;
    write_count = 0;
}