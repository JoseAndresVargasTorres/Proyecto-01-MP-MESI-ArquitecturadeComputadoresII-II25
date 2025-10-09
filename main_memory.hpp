#ifndef MAIN_MEMORY_HPP
#define MAIN_MEMORY_HPP

#include <vector>
#include <cstdint>
#include <stdexcept>
#include <mutex>

class MainMemory {
private:
    std::vector<uint64_t> memory;  // 512 posiciones de 64 bits
    const uint64_t MEM_SIZE_WORDS = 512;
    mutable std::mutex mem_mutex;  // Para acceso thread-safe
    
    mutable uint64_t read_count;
    mutable uint64_t write_count;

    void checkAlignment(uint64_t addr) const;
    void checkBounds(uint64_t addr) const;

public:
    MainMemory();

    // Lectura/escritura de palabras de 64 bits
    void writeWord(uint64_t addr, uint64_t data);
    uint64_t readWord(uint64_t addr) const;
    
    // Lectura/escritura de doubles
    void writeDouble(uint64_t addr, double data);
    double readDouble(uint64_t addr) const;
    
    // Estadísticas
    uint64_t getReadCount() const { return read_count; }
    uint64_t getWriteCount() const { return write_count; }
    void resetStats();
};

#endif // MAIN_MEMORY_HPP