#include "cache.hpp"
#include <algorithm>
#include <iomanip>

// ===============================
// Helpers internos sin cambios
// ===============================
std::optional<uint32_t> Cache2Way::findHit(uint32_t set_idx, uint64_t tg) const {
  const auto& S = sets_[set_idx];
  for (uint32_t w = 0; w < WAYS; ++w) {
    const auto& L = S.ways[w];
    if (L.valid && L.tag == tg) return w;
  }
  return std::nullopt;
}

uint32_t Cache2Way::chooseVictim(uint32_t set_idx) const {
  const auto& S = sets_[set_idx];
  // Preferí una inválida
  for (uint32_t w = 0; w < WAYS; ++w) if (!S.ways[w].valid) return w;
  // LRU: menor last_use
  uint32_t victim = 0;
  uint64_t best   = S.ways[0].last_use;
  for (uint32_t w = 1; w < WAYS; ++w) {
    if (S.ways[w].last_use < best) { best = S.ways[w].last_use; victim = w; }
  }
  return victim;
}

void Cache2Way::writeBackIfDirty(uint32_t set_idx, uint32_t way_idx, uint64_t base_addr) {
  auto& L = sets_[set_idx].ways[way_idx];
  if (L.valid && L.dirty) {
    // Escribe la línea completa (4 palabras de 64 bits)
    for (uint32_t i = 0; i < WORDS_PER_LINE; ++i) {
      uint64_t w = readWordInLine(L, i);
      mem_.write64(base_addr + i * WORD_SIZE, w);
      stats_.mem_writes++;
    }
    stats_.writebacks++;
    L.dirty = false;
  }
}

// NOTA: ya no fija MESI aquí; se fija en load/store
void Cache2Way::fetchLine(uint32_t set_idx, uint32_t way_idx, uint64_t base_addr, uint64_t tg) {
  auto& L = sets_[set_idx].ways[way_idx];
  // Antes de traer, si la víctima está sucia -> write-back
  if (L.valid && L.dirty) {
    uint64_t old_base = ( (L.tag << (INDEX_BITS + OFFSET_BITS)) | (static_cast<uint64_t>(set_idx) << OFFSET_BITS) );
    writeBackIfDirty(set_idx, way_idx, old_base);
  }
  // Traer 32B como 4 palabras de 64b
  for (uint32_t i = 0; i < WORDS_PER_LINE; ++i) {
    uint64_t v = 0;
    mem_.read64(base_addr + i * WORD_SIZE, v);
    stats_.mem_reads++;
    writeWordInLine(L, i, v);
  }
  L.tag     = tg;
  L.valid   = true;
  L.dirty   = false;
  // L.mesi  = (no tocar aquí)
  L.last_use = ++use_tick_;
  stats_.line_fills++;
}

// keep (por compatibilidad con pruebas existentes)
std::pair<uint32_t,bool> Cache2Way::ensureLine(uint64_t addr) {
  const uint32_t set_idx  = index(addr);
  const uint64_t tg       = tag(addr);
  const uint64_t base     = lineBase(addr);

  if (auto h = findHit(set_idx, tg)) {
    auto& L = sets_[set_idx].ways[*h];
    L.last_use = ++use_tick_;
    return { *h, true };
  }
  // Miss: elegir víctima y traer línea
  uint32_t victim = chooseVictim(set_idx);
  fetchLine(set_idx, victim, base, tg);
  return { victim, false };
}

// ===============================
// MESI / Bus: nuevos helpers
// ===============================
int Cache2Way::findLineByBase(uint64_t base_addr) const {
  const uint32_t set_idx = index(base_addr);
  const uint64_t tg      = tag(base_addr);
  const auto& S = sets_[set_idx];
  for (uint32_t w = 0; w < WAYS; ++w) {
    const auto& L = S.ways[w];
    if (L.valid && L.tag == tg) return (int)w;
  }
  return -1;
}

void Cache2Way::snoop(BusMsg msg, uint64_t base_addr) {
  std::scoped_lock lk(mtx_);
  const uint32_t set_idx = index(base_addr);
  int w = findLineByBase(base_addr);
  if (w < 0) return; // no tengo la línea

  auto& L = sets_[set_idx].ways[w];

  auto do_flush = [&](){
    for (uint32_t i=0;i<WORDS_PER_LINE;++i) {
      uint64_t v = readWordInLine(L, i);
      mem_.write64(base_addr + i*WORD_SIZE, v);
      stats_.mem_writes++;
    }
    stats_.writebacks++;
    stats_.snoop_flush++;
  };

  switch (msg) {
    case BusMsg::BusRd:
      if (L.mesi == MESI::M) { do_flush(); L.dirty=false; L.mesi = MESI::S; }
      else if (L.mesi == MESI::E) { L.mesi = MESI::S; stats_.snoop_to_S++; }
      // S/I: no cambian en BusRd
      break;

    case BusMsg::BusRdX:
      if (L.mesi == MESI::M) { do_flush(); L.dirty=false; }
      if (L.mesi != MESI::I) {
        L.mesi = MESI::I; L.valid = false; stats_.snoop_to_I++;
      }
      break;

    case BusMsg::Invalidate:
      if (L.mesi == MESI::M) { do_flush(); L.dirty=false; }
      if (L.mesi != MESI::I) {
        L.mesi = MESI::I; L.valid = false; stats_.snoop_to_I++;
      }
      break;

    case BusMsg::Flush:
      // listeners no accionan; el flush lo ejecuta el emisor
      break;
  }
}

// ===============================
// Accesos de 64 bits con MESI/bus
// ===============================
bool Cache2Way::load64(uint64_t addr, uint64_t& out) {
  if (addr % WORD_SIZE != 0)
    throw std::invalid_argument("Cache64 load: dirección no alineada a 8 bytes");

  uint32_t set_idx, woff, victim;
  uint64_t base, tg;
  bool is_miss = false;
  
  // Fase 1: Verificar hit/miss CON mutex
  {
    std::scoped_lock lk(mtx_);
    set_idx = index(addr);
    woff = wordOffset(addr);
    base = lineBase(addr);
    tg = tag(addr);

    if (auto h = findHit(set_idx, tg)) {
      auto& L = sets_[set_idx].ways[*h];
      L.last_use = ++use_tick_;
      out = readWordInLine(L, woff);
      stats_.hits++;
      return true;  // HIT - salimos temprano
    }
    
    // Es MISS
    is_miss = true;
    victim = chooseVictim(set_idx);
  }
  // Mutex liberado aquí

  // Fase 2: Emitir al bus SIN mutex
  if (is_miss) {
    emit(BusMsg::BusRd, base);
  }

  // Fase 3: Fetch y actualización CON mutex
  {
    std::scoped_lock lk(mtx_);
    fetchLine(set_idx, victim, base, tg);
    auto& L = sets_[set_idx].ways[victim];
    L.mesi = MESI::S;
    out = readWordInLine(L, woff);
    stats_.misses++;
  }

  return false;
}

bool Cache2Way::store64(uint64_t addr, uint64_t value) {
  if (addr % WORD_SIZE != 0)
    throw std::invalid_argument("Cache64 store: dirección no alineada a 8 bytes");

  uint32_t set_idx, woff, victim;
  uint64_t base, tg;
  bool is_hit = false;
  bool need_upgrade = false;
  bool need_fetch = false;
  MESI old_state;

  // Fase 1: Verificar hit/miss CON mutex
  {
    std::scoped_lock lk(mtx_);
    set_idx = index(addr);
    woff = wordOffset(addr);
    base = lineBase(addr);
    tg = tag(addr);

    if (auto h = findHit(set_idx, tg)) {
      auto& L = sets_[set_idx].ways[*h];
      old_state = L.mesi;
      
      if (L.mesi == MESI::S) {
        need_upgrade = true;
      } else if (L.mesi == MESI::E) {
        L.mesi = MESI::M;
      }
      
      writeWordInLine(L, woff, value);
      L.dirty = true;
      L.last_use = ++use_tick_;
      stats_.hits++;
      is_hit = true;
    } else {
      // Es MISS
      need_fetch = true;
      victim = chooseVictim(set_idx);
    }
  }
  // Mutex liberado aquí

  // Fase 2: Emitir al bus SIN mutex
  if (need_upgrade) {
    emit(BusMsg::BusRdX, base);
    
    std::scoped_lock lk(mtx_);
    if (auto h = findHit(set_idx, tg)) {
      sets_[set_idx].ways[*h].mesi = MESI::M;
    }
    return true;
  }
  
  if (need_fetch) {
    emit(BusMsg::BusRdX, base);
    
    std::scoped_lock lk(mtx_);
    fetchLine(set_idx, victim, base, tg);
    auto& L = sets_[set_idx].ways[victim];
    writeWordInLine(L, woff, value);
    L.dirty = true;
    L.mesi = MESI::M;
    L.last_use = ++use_tick_;
    stats_.misses++;
    return false;
  }

  return is_hit;
}

// ===============================
// Wrappers double / flush / dump
// ===============================
bool Cache2Way::loadDouble(uint64_t addr, double& out) {
  uint64_t bits = 0;
  bool hit = load64(addr, bits);
  std::memcpy(&out, &bits, sizeof(bits));
  return hit;
}

bool Cache2Way::storeDouble(uint64_t addr, double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return store64(addr, bits);
}

void Cache2Way::flushAll() {
  std::scoped_lock lk(mtx_);
  for (uint32_t s = 0; s < SETS; ++s) {
    for (uint32_t w = 0; w < WAYS; ++w) {
      auto& L = sets_[s].ways[w];
      if (L.valid && L.dirty) {
        uint64_t base = ( (L.tag << (INDEX_BITS + OFFSET_BITS)) | (static_cast<uint64_t>(s) << OFFSET_BITS) );
        writeBackIfDirty(s, w, base);
      }
    }
  }
}

void Cache2Way::dump(std::ostream& os) const {
  std::scoped_lock lk(mtx_);
  os << "Cache2Way dump (SETS=" << SETS << ", WAYS=" << WAYS << ")\n";
  for (uint32_t s = 0; s < SETS; ++s) {
    os << "Set " << s << ":\n";
    for (uint32_t w = 0; w < WAYS; ++w) {
      const auto& L = sets_[s].ways[w];
      os << "  Way " << w
         << " | V=" << L.valid
         << " D=" << L.dirty
         << " MESI=" << static_cast<int>(L.mesi)
         << " Tag=0x" << std::hex << L.tag << std::dec
         << " LRU=" << L.last_use
         << "\n";
    }
  }
  auto st = stats_;
  os << "Stats: hits=" << st.hits
     << " misses=" << st.misses
     << " fills=" << st.line_fills
     << " wbs=" << st.writebacks
     << " memR=" << st.mem_reads
     << " memW=" << st.mem_writes
     << " | busRd=" << st.bus_rd
     << " busRdX=" << st.bus_rdx
     << " busInv=" << st.bus_inv
     << " | snoopI=" << st.snoop_to_I
     << " snoopS=" << st.snoop_to_S
     << " snoopFlush=" << st.snoop_flush
     << "\n";
}

void Cache2Way::invalidateAll() {
  std::scoped_lock lk(mtx_);
  for (uint32_t s = 0; s < SETS; ++s) {
    for (uint32_t w = 0; w < WAYS; ++w) {
      auto& L = sets_[s].ways[w];
      L.valid = false;
      L.dirty = false;
      L.mesi  = MESI::I;
      L.tag   = 0;
      L.last_use = 0;
    }
  }
}

std::optional<Cache2Way::MESI> Cache2Way::getLineMESI(uint64_t addr) const {
  std::scoped_lock lk(mtx_);
  const uint64_t base = lineBase(addr);
  int w = findLineByBase(base);
  if (w < 0) return std::nullopt;
  const uint32_t set_idx = index(base);
  const auto& L = sets_[set_idx].ways[w];
  if (!L.valid) return std::nullopt;
  return L.mesi;
}