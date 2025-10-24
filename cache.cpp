#include "cache.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

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
  for (uint32_t w = 0; w < WAYS; ++w) if (!S.ways[w].valid) return w;
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
    for (uint32_t i = 0; i < WORDS_PER_LINE; ++i) {
      uint64_t w = readWordInLine(L, i);
      mem_.write64(base_addr + i * WORD_SIZE, w);
      stats_.mem_writes++;
    }
    stats_.writebacks++;
    L.dirty = false;
  }
}

void Cache2Way::fetchLine(uint32_t set_idx, uint32_t way_idx, uint64_t base_addr, uint64_t tg) {
  auto& L = sets_[set_idx].ways[way_idx];
  if (L.valid && L.dirty) {
    uint64_t old_base = ( (L.tag << (INDEX_BITS + OFFSET_BITS)) | (static_cast<uint64_t>(set_idx) << OFFSET_BITS) );
    writeBackIfDirty(set_idx, way_idx, old_base);
  }
  for (uint32_t i = 0; i < WORDS_PER_LINE; ++i) {
    uint64_t v = 0;
    mem_.read64(base_addr + i * WORD_SIZE, v);
    stats_.mem_reads++;
    writeWordInLine(L, i, v);
  }
  L.tag     = tg;
  L.valid   = true;
  L.dirty   = false;
  L.last_use = ++use_tick_;
  stats_.line_fills++;
}

std::pair<uint32_t,bool> Cache2Way::ensureLine(uint64_t addr) {
  const uint32_t set_idx  = index(addr);
  const uint64_t tg       = tag(addr);
  const uint64_t base     = lineBase(addr);

  if (auto h = findHit(set_idx, tg)) {
    auto& L = sets_[set_idx].ways[*h];
    L.last_use = ++use_tick_;
    return { *h, true };
  }
  uint32_t victim = chooseVictim(set_idx);
  fetchLine(set_idx, victim, base, tg);
  return { victim, false };
}

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
  if (w < 0) return;

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
      if (L.mesi == MESI::M) { 
        do_flush(); 
        L.dirty=false; 
        L.mesi = MESI::S;  // De M a S
        std::ostringstream oss;
        oss << "[C" << id_ << "] Snoop BusRd: M->S (flush) addr=0x" << std::hex << base_addr << std::dec;
        logMESI(oss.str());
      }
      else if (L.mesi == MESI::E) { 
        L.mesi = MESI::S; // De E a S
        stats_.snoop_to_S++;
        std::ostringstream oss;
        oss << "[C" << id_ << "] Snoop BusRd: E->S addr=0x" << std::hex << base_addr << std::dec;
        logMESI(oss.str());
      }
      break;

    case BusMsg::BusRdX:
      if (L.mesi == MESI::M) { 
        do_flush(); 
        L.dirty=false;
        std::ostringstream oss;
        oss << "[C" << id_ << "] Snoop BusRdX: M->I (flush) addr=0x" << std::hex << base_addr << std::dec;
        logMESI(oss.str());
      }
      else if (L.mesi == MESI::E) {
        std::ostringstream oss;
        oss << "[C" << id_ << "] Snoop BusRdX: E->I addr=0x" << std::hex << base_addr << std::dec;
        logMESI(oss.str());
      }
      else if (L.mesi == MESI::S) {
        std::ostringstream oss;
        oss << "[C" << id_ << "] Snoop BusRdX: S->I addr=0x" << std::hex << base_addr << std::dec;
        logMESI(oss.str());
      }
      if (L.mesi != MESI::I) {
        L.mesi = MESI::I; 
        L.valid = false; 
        stats_.snoop_to_I++;
      }
      break;

    case BusMsg::Invalidate:
      if (L.mesi == MESI::M) { 
        do_flush(); 
        L.dirty=false;
        std::ostringstream oss;
        oss << "[C" << id_ << "] Snoop Invalidate: M->I (flush) addr=0x" << std::hex << base_addr << std::dec;
        logMESI(oss.str());
      }
      else if (L.mesi == MESI::E) {
        std::ostringstream oss;
        oss << "[C" << id_ << "] Snoop Invalidate: E->I addr=0x" << std::hex << base_addr << std::dec;
        logMESI(oss.str());
      }
      else if (L.mesi == MESI::S) {
        std::ostringstream oss;
        oss << "[C" << id_ << "] Snoop Invalidate: S->I addr=0x" << std::hex << base_addr << std::dec;
        logMESI(oss.str());
      }
      if (L.mesi != MESI::I) {
        L.mesi = MESI::I; 
        L.valid = false; 
        stats_.snoop_to_I++;
      }
      break;

    case BusMsg::Flush:
      break;
  }
}

bool Cache2Way::load64(uint64_t addr, uint64_t& out) {
  if (addr % WORD_SIZE != 0)
    throw std::invalid_argument("Cache64 load: dirección no alineada a 8 bytes");

  uint32_t set_idx, woff, victim;
  uint64_t base, tg;
  bool is_miss = false;
  
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
      
      std::ostringstream oss;
      oss << "[C" << id_ << "] LOAD HIT addr=0x" << std::hex << addr 
          << " estado=" << mesiName(L.mesi) << std::dec;
      logMESI(oss.str());
      return true;
    }
    
    is_miss = true;
    victim = chooseVictim(set_idx);
  }

  if (is_miss) {
    emit(BusMsg::BusRd, base);
  }

  {
    std::scoped_lock lk(mtx_);
    fetchLine(set_idx, victim, base, tg);
    auto& L = sets_[set_idx].ways[victim];
    L.mesi = MESI::E;
    out = readWordInLine(L, woff);
    stats_.misses++;
    
    std::ostringstream oss;
    oss << "[C" << id_ << "] LOAD MISS -> E addr=0x" << std::hex << base << std::dec;
    logMESI(oss.str());
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

  {
    std::scoped_lock lk(mtx_);
    set_idx = index(addr);
    woff = wordOffset(addr);
    base = lineBase(addr);
    tg = tag(addr);

    if (auto h = findHit(set_idx, tg)) {
      auto& L = sets_[set_idx].ways[*h];
      
      if (L.mesi == MESI::S) {
        std::ostringstream oss;
        oss << "[C" << id_ << "] STORE en S -> need upgrade to M addr=0x" << std::hex << addr << std::dec;
        logMESI(oss.str());
        need_upgrade = true;
      } else if (L.mesi == MESI::E) {
        L.mesi = MESI::M;
        std::ostringstream oss;
        oss << "[C" << id_ << "] STORE E->M addr=0x" << std::hex << addr << std::dec;
        logMESI(oss.str());
      } else if (L.mesi == MESI::M) {
        std::ostringstream oss;
        oss << "[C" << id_ << "] STORE en M (ya modificado) addr=0x" << std::hex << addr << std::dec;
        logMESI(oss.str());
      }
      
      writeWordInLine(L, woff, value);
      L.dirty = true;
      L.last_use = ++use_tick_;
      stats_.hits++;
      is_hit = true;
    } else {
      need_fetch = true;
      victim = chooseVictim(set_idx);
    }
  }

  if (need_upgrade) {
    emit(BusMsg::BusRdX, base);
    
    std::scoped_lock lk(mtx_);
    if (auto h = findHit(set_idx, tg)) {
      sets_[set_idx].ways[*h].mesi = MESI::M;
      std::ostringstream oss;
      oss << "[C" << id_ << "] STORE upgrade: S->M addr=0x" << std::hex << base << std::dec;
      logMESI(oss.str());
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
    
    std::ostringstream oss;
    oss << "[C" << id_ << "] STORE MISS -> M addr=0x" << std::hex << base << std::dec;
    logMESI(oss.str());
    return false;
  }

  return is_hit;
}

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

Cache2Way::LineInfo Cache2Way::getLineInfo(uint32_t set_idx, uint32_t way_idx) const {
  std::scoped_lock lk(mtx_);
  
  if (set_idx >= SETS || way_idx >= WAYS) {
    throw std::out_of_range("getLineInfo: índices fuera de rango");
  }
  
  const auto& L = sets_[set_idx].ways[way_idx];
  
  LineInfo info;
  info.tag = L.tag;
  info.valid = L.valid;
  info.dirty = L.dirty;
  info.mesi = L.mesi;
  info.last_use = L.last_use;
  
  return info;
}
