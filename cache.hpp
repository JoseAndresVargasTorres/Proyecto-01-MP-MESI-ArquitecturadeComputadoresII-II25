#pragma once
#include <cstdint>
#include <array>
#include <mutex>
#include <cstring>
#include <optional>
#include <ostream>
#include <stdexcept>
#include "interconnect.hpp"  // ← NUEVO: bus + IBusClient

/// Interfaz mínima para memoria principal.
/// Adaptá este wrapper a tu clase MainMemory real.
struct IMainMemory {
  virtual ~IMainMemory() = default;
  virtual void read64(uint64_t addr, uint64_t& out) = 0;   // Lee 8 bytes alineados
  virtual void write64(uint64_t addr, uint64_t value) = 0; // Escribe 8 bytes alineados
};

/// Caché 2-way, 16 líneas, 32B por línea, write-allocate + write-back.
/// Pensada para accesos de 64 bits (enteros o double).
class Cache2Way : public IBusClient {  // ← NUEVO: implementa IBusClient
public:
  static constexpr uint32_t LINE_SIZE_BYTES = 32;     // 32 B
  static constexpr uint32_t NUM_LINES       = 16;     // total lines
  static constexpr uint32_t WAYS            = 2;      // 2-way
  static constexpr uint32_t SETS            = NUM_LINES / WAYS; // 8 sets
  static constexpr uint32_t OFFSET_BITS     = 5;      // log2(32) = 5
  static constexpr uint32_t INDEX_BITS      = 3;      // log2(8)  = 3
  static constexpr uint64_t OFFSET_MASK     = (1ull << OFFSET_BITS) - 1;  // 0..31
  static constexpr uint64_t INDEX_MASK      = (1ull << INDEX_BITS) - 1;   // 0..7
  static constexpr uint32_t WORD_SIZE       = 8;      // 8 B (64 bits)
  static constexpr uint32_t WORDS_PER_LINE  = LINE_SIZE_BYTES / WORD_SIZE; // 4 palabras de 64b por línea

  enum class MESI : uint8_t { I=0, S, E, M };

  struct Stats {
    // caché
    uint64_t hits        = 0;
    uint64_t misses      = 0;
    uint64_t line_fills  = 0;  // líneas traídas de memoria
    uint64_t writebacks  = 0;  // líneas sucias escritas a memoria
    uint64_t mem_reads   = 0;  // lecturas de 64b a memoria (para fill)
    uint64_t mem_writes  = 0;  // escrituras de 64b a memoria (para write-back)
    // bus (NUEVO)
    uint64_t bus_rd      = 0;  // BusRd emitidos
    uint64_t bus_rdx     = 0;  // BusRdX emitidos
    uint64_t bus_inv     = 0;  // Invalidate emitidos
    // reacciones a snoop (NUEVO)
    uint64_t snoop_to_I  = 0;  // líneas invalidadas por snoop
    uint64_t snoop_to_S  = 0;  // líneas degradadas a S por snoop
    uint64_t snoop_flush = 0;  // flush en respuesta a snoop
  };

  explicit Cache2Way(IMainMemory& mem) : mem_(mem) {}

  // Conexión al bus (NUEVO)
  void setBus(Interconnect* b) { std::scoped_lock lk(mtx_); bus_ = b; }

  // Devuelve true si fue hit.
  bool load64(uint64_t addr, uint64_t& out);
  bool store64(uint64_t addr, uint64_t value);

  // Helpers para double (bit-exacto)
  bool loadDouble(uint64_t addr, double& out);
  bool storeDouble(uint64_t addr, double value);

  // Escribe a memoria todas las líneas sucias.
  void flushAll();

  // Limpia estadísticas.
  void resetStats() { std::scoped_lock lk(mtx_); stats_ = {}; }

  Stats getStats() const { std::scoped_lock lk(mtx_); return stats_; }

  // Dump de estado (tags/valid/dirty/MESI por set/way).
  void dump(std::ostream& os) const;

  // Invalidar todo el contenido de la caché (para pruebas)
  void invalidateAll();

  // IBusClient (NUEVO): reacción a mensajes de bus
  void snoop(BusMsg msg, uint64_t base_addr) override;

private:
  struct Line {
    uint64_t tag    = 0;
    bool     valid  = false;
    bool     dirty  = false;
    MESI     mesi   = MESI::I;
    uint64_t last_use = 0;  // para LRU (contador global)
    std::array<uint8_t, LINE_SIZE_BYTES> data{}; // 32 bytes
  };

  struct Set {
    std::array<Line, WAYS> ways;
  };

  // Helpers de dirección
  static inline uint64_t lineBase(uint64_t addr)   { return addr & ~OFFSET_MASK; }
  static inline uint32_t offset(uint64_t addr)     { return static_cast<uint32_t>(addr & OFFSET_MASK); }
  static inline uint32_t index(uint64_t addr)      { return static_cast<uint32_t>((addr >> OFFSET_BITS) & INDEX_MASK); }
  static inline uint64_t tag(uint64_t addr)        { return addr >> (OFFSET_BITS + INDEX_BITS); }
  static inline uint32_t wordOffset(uint64_t addr) { return static_cast<uint32_t>((offset(addr)) / WORD_SIZE); }

  // Busca hit; si no hay, retorna way vacío.
  std::optional<uint32_t> findHit(uint32_t set_idx, uint64_t tag) const;

  // Elige víctima en set (preferir inválida; si no, LRU).
  uint32_t chooseVictim(uint32_t set_idx) const;

  // Trae la línea desde memoria (line fill).
  void fetchLine(uint32_t set_idx, uint32_t way_idx, uint64_t base_addr, uint64_t tg);

  // Si la línea está sucia, la escribe a memoria.
  void writeBackIfDirty(uint32_t set_idx, uint32_t way_idx, uint64_t base_addr);

  // Asegura que la línea esté presente; retorna par (way_idx, hit).
  // (Úsala si querés, pero para MESI conviene controlar estados en load/store directamente)
  std::pair<uint32_t,bool> ensureLine(uint64_t addr);

  // Escribe/lee palabra de 64b en la línea ya presente.
  static inline uint64_t readWordInLine(const Line& L, uint32_t word_off) {
    uint64_t v = 0;
    std::memcpy(&v, &L.data[word_off * WORD_SIZE], WORD_SIZE);
    return v;
  }
  static inline void writeWordInLine(Line& L, uint32_t word_off, uint64_t v) {
    std::memcpy(&L.data[word_off * WORD_SIZE], &v, WORD_SIZE);
  }

  // --- MESI / Bus helpers (NUEVO) ---
  // Encuentra la línea por base de línea (alineada a 32B) en este caché; -1 si no está
  int findLineByBase(uint64_t base_addr) const;

  // Emite mensaje al bus (si existe) y actualiza counters
  inline void emit(BusMsg m, uint64_t base_addr) {
    if (!bus_) return;
    switch (m) {
      case BusMsg::BusRd:       stats_.bus_rd++;  break;
      case BusMsg::BusRdX:      stats_.bus_rdx++; break;
      case BusMsg::Invalidate:  stats_.bus_inv++; break;
      default: break;
    }
    bus_->broadcast(this, m, base_addr);
  }

private:
  IMainMemory& mem_;
  mutable std::mutex mtx_;
  std::array<Set, SETS> sets_{};
  mutable uint64_t use_tick_ = 0; // contador global para LRU
  Stats stats_{};
  Interconnect* bus_ = nullptr;   // ← NUEVO: conexión al bus
};
