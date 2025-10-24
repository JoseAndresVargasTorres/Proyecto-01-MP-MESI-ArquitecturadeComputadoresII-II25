#pragma once
#include <cstdint>
#include <array>
#include <mutex>
#include <cstring>
#include <optional>
#include <ostream>
#include <sstream>  // ← AGREGADO: necesario para std::ostringstream
#include <stdexcept>
#include <functional>
#include "interconnect.hpp"

/// Interfaz mínima para memoria principal.
struct IMainMemory {
  virtual ~IMainMemory() = default;
  virtual void read64(uint64_t addr, uint64_t& out) = 0;
  virtual void write64(uint64_t addr, uint64_t value) = 0;
};

/// Caché 2-way, 16 líneas, 32B por línea, write-allocate + write-back.
class Cache2Way : public IBusClient {
public:
  static constexpr uint32_t LINE_SIZE_BYTES = 32;
  static constexpr uint32_t NUM_LINES       = 16;
  static constexpr uint32_t WAYS            = 2;
  static constexpr uint32_t SETS            = NUM_LINES / WAYS;
  static constexpr uint32_t OFFSET_BITS     = 5;
  static constexpr uint32_t INDEX_BITS      = 3;
  static constexpr uint64_t OFFSET_MASK     = (1ull << OFFSET_BITS) - 1;
  static constexpr uint64_t INDEX_MASK      = (1ull << INDEX_BITS) - 1;
  static constexpr uint32_t WORD_SIZE       = 8;
  static constexpr uint32_t WORDS_PER_LINE  = LINE_SIZE_BYTES / WORD_SIZE;

  enum class MESI : uint8_t { I=0, S, E, M };

  struct Stats {
    uint64_t hits        = 0;
    uint64_t misses      = 0;
    uint64_t line_fills  = 0;
    uint64_t writebacks  = 0;
    uint64_t mem_reads   = 0;
    uint64_t mem_writes  = 0;
    uint64_t bus_rd      = 0;
    uint64_t bus_rdx     = 0;
    uint64_t bus_inv     = 0;
    uint64_t snoop_to_I  = 0;
    uint64_t snoop_to_S  = 0;
    uint64_t snoop_flush = 0;
  };

  struct LineInfo {
    uint64_t tag;
    bool valid;
    bool dirty;
    MESI mesi;
    uint64_t last_use;
  };

  // Callback para notificar eventos MESI a la GUI
  using LogCallback = std::function<void(const std::string&)>;

  explicit Cache2Way(IMainMemory& mem) : mem_(mem) {}

  void setId(int id) { std::scoped_lock lk(mtx_); id_ = id; }
  void setBus(Interconnect* b) { std::scoped_lock lk(mtx_); bus_ = b; }
  void setLogCallback(LogCallback cb) { log_callback_ = cb; }

  bool load64(uint64_t addr, uint64_t& out);
  bool store64(uint64_t addr, uint64_t value);
  bool loadDouble(uint64_t addr, double& out);
  bool storeDouble(uint64_t addr, double value);

  void flushAll();
  void invalidateAll();
  void resetStats() { std::scoped_lock lk(mtx_); stats_ = {}; }
  Stats getStats() const { std::scoped_lock lk(mtx_); return stats_; }
  void dump(std::ostream& os) const;

  std::optional<MESI> getLineMESI(uint64_t addr) const;
  LineInfo getLineInfo(uint32_t set_idx, uint32_t way_idx) const;

  void snoop(BusMsg msg, uint64_t base_addr) override;

private:
  struct Line {
    uint64_t tag    = 0;
    bool     valid  = false;
    bool     dirty  = false;
    MESI     mesi   = MESI::I;
    uint64_t last_use = 0;
    std::array<uint8_t, LINE_SIZE_BYTES> data{};  // Datos de la línea de caché
  };
  struct Set { std::array<Line, WAYS> ways; };

  static inline uint64_t lineBase(uint64_t addr)   { return addr & ~OFFSET_MASK; }
  static inline uint32_t offset(uint64_t addr)     { return static_cast<uint32_t>(addr & OFFSET_MASK); }
  static inline uint32_t index(uint64_t addr)      { return static_cast<uint32_t>((addr >> OFFSET_BITS) & INDEX_MASK); }
  static inline uint64_t tag(uint64_t addr)        { return addr >> (OFFSET_BITS + INDEX_BITS); }
  static inline uint32_t wordOffset(uint64_t addr) { return static_cast<uint32_t>((offset(addr)) / WORD_SIZE); }

  std::optional<uint32_t> findHit(uint32_t set_idx, uint64_t tag) const;
  uint32_t chooseVictim(uint32_t set_idx) const;
  void fetchLine(uint32_t set_idx, uint32_t way_idx, uint64_t base_addr, uint64_t tg);
  void writeBackIfDirty(uint32_t set_idx, uint32_t way_idx, uint64_t base_addr);
  std::pair<uint32_t,bool> ensureLine(uint64_t addr);

  static inline uint64_t readWordInLine(const Line& L, uint32_t word_off) {
    uint64_t v = 0;
    std::memcpy(&v, &L.data[word_off * WORD_SIZE], WORD_SIZE);
    return v;
  }
  static inline void writeWordInLine(Line& L, uint32_t word_off, uint64_t v) {
    std::memcpy(&L.data[word_off * WORD_SIZE], &v, WORD_SIZE);
  }

  int findLineByBase(uint64_t base_addr) const;
  static const char* mesiName(MESI m) {
    switch (m) {
      case MESI::M: return "M";
      case MESI::E: return "E";
      case MESI::S: return "S";
      default: return "I";
    }
  }

  void logMESI(const std::string& msg) {
    if (log_callback_) {
      log_callback_(msg);
    }
  }

  inline void emit(BusMsg m, uint64_t base_addr) {
    if (!bus_) return;
    switch (m) {
      case BusMsg::BusRd:       stats_.bus_rd++;  break;
      case BusMsg::BusRdX:      stats_.bus_rdx++; break;
      case BusMsg::Invalidate:  stats_.bus_inv++; break;
      default: break;
    }
    
    std::ostringstream oss;
    oss << "[BUS] ";
    if (m == BusMsg::BusRd) oss << "BusRd";
    else if (m == BusMsg::BusRdX) oss << "BusRdX";
    else if (m == BusMsg::Invalidate) oss << "Invalidate";
    else oss << "Flush";
    oss << " emitido por C" << id_ << " (addr=0x" << std::hex << base_addr << std::dec << ")";
    logMESI(oss.str());

    bus_->broadcast(this, m, base_addr);
  }

private:
  IMainMemory& mem_;
  mutable std::mutex mtx_;
  std::array<Set, SETS> sets_{};  // Almacenamiento de las líneas de caché
  mutable uint64_t use_tick_ = 0;
  Stats stats_{};  // Estadísticas de la caché
  Interconnect* bus_ = nullptr;  // Bus de comunicación
  int id_ = -1;  // ID de la caché
  LogCallback log_callback_;  // Callback para logs
};
