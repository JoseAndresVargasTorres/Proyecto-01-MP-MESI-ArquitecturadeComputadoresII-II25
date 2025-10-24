// gui.cpp
// Implementación de la interfaz gráfica para el simulador MESI
// CE-4302 Arquitectura de Computadores II

#include "gui.hpp"
#include "main_memory.hpp"
#include "memory_adapter.hpp"
#include "cache.hpp"
#include "processing_element.hpp"
#include "interconnect.hpp"

#include <FL/fl_draw.H>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <thread>

MESISimulatorGUI* g_gui_instance = nullptr;

// ============================================================================
// CacheLineWidget - Widget para mostrar una línea de caché
// ============================================================================

CacheLineWidget::CacheLineWidget(int x, int y, int w, int h, const char* label)
    : Fl_Box(x, y, w, h, label),
      set_(0), way_(0), tag_(0), valid_(false), dirty_(false),
      mesi_state_(0), lru_(0) {
    box(FL_BORDER_BOX);
}

void CacheLineWidget::setLineData(int set, int way, uint64_t tag, bool valid, 
                                   bool dirty, int mesi_state, uint64_t lru) {
    set_ = set;
    way_ = way;
    tag_ = tag;
    valid_ = valid;
    dirty_ = dirty;
    mesi_state_ = mesi_state;
    lru_ = lru;
    redraw();
}

void CacheLineWidget::draw() {
    Fl_Color bg_color;
    switch (mesi_state_) {
        case 3: bg_color = BG_MODIFIED; break;
        case 2: bg_color = BG_EXCLUSIVE; break;
        case 1: bg_color = BG_SHARED; break;
        default: bg_color = BG_INVALID; break;
    }
    
    fl_color(bg_color);
    fl_rectf(x(), y(), w(), h());
    
    fl_color(FL_BLACK);
    fl_rect(x(), y(), w(), h());
    
    fl_color(FL_BLACK);
    fl_font(FL_COURIER, 11);
    
    std::ostringstream oss;
    oss << "S" << set_ << "W" << way_ << " ";
    
    if (valid_) {
        oss << "T:0x" << std::hex << std::setfill('0') << std::setw(4) << tag_ << " ";
        
        const char* mesi_names[] = {"I", "S", "E", "M"};
        oss << mesi_names[mesi_state_] << " ";
        
        oss << (dirty_ ? "D" : "-") << " ";
        oss << "LRU:" << std::dec << lru_;
    } else {
        oss << "--- INVALID ---";
    }
    
    std::string text = oss.str();
    fl_draw(text.c_str(), x() + 5, y(), w() - 10, h(), FL_ALIGN_LEFT | FL_ALIGN_CENTER);
}

// ============================================================================
// RegisterWidget - Widget para mostrar registros de un PE
// ============================================================================

RegisterWidget::RegisterWidget(int x, int y, int w, int h, const char* label)
    : Fl_Box(x, y, w, h, label), pc_(0), finished_(false) {
    box(FL_BORDER_BOX);
    for (int i = 0; i < 8; i++) {
        registers_[i] = 0;
    }
}

void RegisterWidget::setRegisters(const uint64_t* regs, int pc, bool finished) {
    for (int i = 0; i < 8; i++) {
        registers_[i] = regs[i];
    }
    pc_ = pc;
    finished_ = finished;
    redraw();
}

void RegisterWidget::draw() {
    fl_color(finished_ ? fl_rgb_color(200, 255, 200) : FL_WHITE);
    fl_rectf(x(), y(), w(), h());
    
    fl_color(FL_BLACK);
    fl_rect(x(), y(), w(), h());
    
    fl_color(FL_BLACK);
    fl_font(FL_HELVETICA_BOLD, 12);
    fl_draw(label(), x() + 5, y() + 15);
    
    fl_font(FL_COURIER, 11);
    int ypos = y() + 35;
    
    for (int i = 0; i < 8; i++) {
        std::ostringstream oss;
        oss << "R" << i << ": 0x" << std::hex << std::setfill('0') 
            << std::setw(16) << registers_[i];
        fl_draw(oss.str().c_str(), x() + 5, ypos);
        ypos += 18;
    }
    
    ypos += 5;
    std::ostringstream pc_oss;
    pc_oss << "PC: " << std::dec << pc_;
    fl_draw(pc_oss.str().c_str(), x() + 5, ypos);
    
    ypos += 20;
    fl_font(FL_HELVETICA_BOLD, 11);
    if (finished_) {
        fl_color(FL_RED);
        fl_draw("FINISHED", x() + 5, ypos);
    } else {
        fl_color(FL_GREEN);
        fl_draw("RUNNING", x() + 5, ypos);
    }
}

// ============================================================================
// BusLogWidget - Widget para el log del bus
// ============================================================================

BusLogWidget::BusLogWidget(int x, int y, int w, int h, const char* label)
    : Fl_Text_Display(x, y, w, h, label) {
    buffer_ = new Fl_Text_Buffer();
    buffer(buffer_);
    textfont(FL_COURIER);
    textsize(9);
}

void BusLogWidget::addMessage(const std::string& msg) {
    buffer_->append(msg.c_str());
    buffer_->append("\n");
    
    if (buffer_->count_lines(0, buffer_->length()) > MAX_LINES) {
        int end_of_first_line = buffer_->line_end(0);
        buffer_->remove(0, end_of_first_line + 1);
    }
    
    insert_position(buffer_->length());
    show_insert_position();
}

void BusLogWidget::clear() {
    buffer_->text("");
}

// ============================================================================
// CacheStatsWidget - Widget para estadísticas de caché
// ============================================================================

CacheStatsWidget::CacheStatsWidget(int x, int y, int w, int h, const char* label)
    : Fl_Box(x, y, w, h, label),
      hits_(0), misses_(0), line_fills_(0), writebacks_(0),
      bus_rd_(0), bus_rdx_(0), bus_inv_(0),
      snoop_i_(0), snoop_s_(0), snoop_flush_(0) {
    box(FL_BORDER_BOX);
    align(FL_ALIGN_TOP_LEFT | FL_ALIGN_INSIDE);
}

void CacheStatsWidget::setStats(uint64_t hits, uint64_t misses, uint64_t line_fills,
                                 uint64_t writebacks, uint64_t bus_rd, uint64_t bus_rdx,
                                 uint64_t bus_inv, uint64_t snoop_i, uint64_t snoop_s,
                                 uint64_t snoop_flush) {
    hits_ = hits;
    misses_ = misses;
    line_fills_ = line_fills;
    writebacks_ = writebacks;
    bus_rd_ = bus_rd;
    bus_rdx_ = bus_rdx;
    bus_inv_ = bus_inv;
    snoop_i_ = snoop_i;
    snoop_s_ = snoop_s;
    snoop_flush_ = snoop_flush;
    redraw();
}

void CacheStatsWidget::draw() {
    fl_color(FL_WHITE);
    fl_rectf(x(), y(), w(), h());
    
    fl_color(FL_BLACK);
    fl_rect(x(), y(), w(), h());
    
    fl_color(FL_BLACK);
    fl_font(FL_COURIER, 10);
    
    int ypos = y() + 15;
    int line_height = 14;
    
    auto draw_line = [&](const std::string& text) {
        fl_draw(text.c_str(), x() + 5, ypos);
        ypos += line_height;
    };
    
    std::ostringstream oss;
    oss << "Hits: " << hits_;
    draw_line(oss.str());
    
    oss.str(""); oss << "Misses: " << misses_;
    draw_line(oss.str());
    
    oss.str(""); oss << "Fills: " << line_fills_;
    draw_line(oss.str());
    
    oss.str(""); oss << "WBs: " << writebacks_;
    draw_line(oss.str());
    
    ypos += 5;
    oss.str(""); oss << "BusRd: " << bus_rd_;
    draw_line(oss.str());
    
    oss.str(""); oss << "BusRdX: " << bus_rdx_;
    draw_line(oss.str());
    
    oss.str(""); oss << "BusInv: " << bus_inv_;
    draw_line(oss.str());
    
    ypos += 5;
    oss.str(""); oss << "Snp->I: " << snoop_i_;
    draw_line(oss.str());
    
    oss.str(""); oss << "Snp->S: " << snoop_s_;
    draw_line(oss.str());
    
    oss.str(""); oss << "SnpFls: " << snoop_flush_;
    draw_line(oss.str());
}

// ============================================================================
// MESISimulatorGUI - Constructor
// ============================================================================

MESISimulatorGUI::MESISimulatorGUI(int width, int height)
    : vector_size_(16),
      breakpoint_interval_(0),
      system_loaded_(false),
      running_(false),
      stepping_(false),
      global_step_count_(0),
      current_pe_for_step_(0),
      pe_alive_(4, true),
      all_pes_finished_(false) {
    
    g_gui_instance = this;
    
    window_ = new Fl_Window(width, height, "Multiprocessor System with MESI Protocol - CE4302");
    window_->begin();
    
    createControlPanel();
    createPEPanel();
    createCachePanel();
    createBusPanel();
    createStatsPanel();
    
    window_->end();
    window_->resizable(window_);
    
    Fl::add_timeout(0.1, cb_update_display, this);
}

MESISimulatorGUI::~MESISimulatorGUI() {
    running_ = false;
    for (auto& t : pe_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    if (window_) {
        delete window_;
    }
    
    g_gui_instance = nullptr;
}

// ============================================================================
// Creación de paneles de la interfaz gráfica
// ============================================================================

void MESISimulatorGUI::createControlPanel() {
    int panel_height = 60;
    control_panel_ = new Fl_Group(0, 0, window_->w(), panel_height);
    control_panel_->box(FL_UP_BOX);
    control_panel_->begin();
    
    int x = 10;
    int y = 10;
    int btn_width = 100;
    int btn_height = 40;
    int spacing = 10;
    
    btn_load_ = new Fl_Button(x, y, btn_width, btn_height, "Load System");
    btn_load_->callback(cb_load_system, this);
    x += btn_width + spacing;
    
    btn_step_ = new Fl_Button(x, y, btn_width, btn_height, "Step");
    btn_step_->callback(cb_step, this);
    btn_step_->deactivate();
    x += btn_width + spacing;
    
    btn_continue_ = new Fl_Button(x, y, btn_width, btn_height, "Continue");
    btn_continue_->callback(cb_continue, this);
    btn_continue_->deactivate();
    x += btn_width + spacing;
    
    btn_run_all_ = new Fl_Button(x, y, btn_width, btn_height, "Run All");
    btn_run_all_->callback(cb_run_all, this);
    btn_run_all_->deactivate();
    x += btn_width + spacing;
    
    btn_reset_ = new Fl_Button(x, y, btn_width, btn_height, "Reset");
    btn_reset_->callback(cb_reset, this);
    x += btn_width + spacing;
    
    btn_exit_ = new Fl_Button(x, y, btn_width, btn_height, "Exit");
    btn_exit_->callback(cb_exit, this);
    x += btn_width + spacing * 3;
    
    status_box_ = new Fl_Box(x, y, 400, btn_height, "Ready. Load system to begin.");
    status_box_->box(FL_DOWN_BOX);
    status_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    
    control_panel_->end();
}

void MESISimulatorGUI::createPEPanel() {
    int panel_y = 60;
    int panel_height = 220;
    
    pe_panel_ = new Fl_Group(0, panel_y, window_->w(), panel_height, "Processing Elements");
    pe_panel_->box(FL_DOWN_BOX);
    pe_panel_->align(FL_ALIGN_TOP_LEFT | FL_ALIGN_INSIDE);
    pe_panel_->begin();
    
    int pe_width = 180;
    int pe_height = 200;
    int x_start = 10;
    int y_start = panel_y + 20;
    int spacing = 10;
    
    for (int i = 0; i < 4; i++) {
        int x = x_start + i * (pe_width + spacing);
        char label[32];
        snprintf(label, sizeof(label), "PE%d", i);
        
        pe_widgets_[i] = new RegisterWidget(x, y_start, pe_width, pe_height, label);
    }
    
    pe_panel_->end();
}

void MESISimulatorGUI::createCachePanel() {
    int panel_y = 280;
    int panel_width = window_->w() * 3 / 4;
    int panel_height = window_->h() - panel_y - 10;
    
    cache_scroll_ = new Fl_Scroll(0, panel_y, panel_width, panel_height, "Cache Lines (2-Way Set Associative)");
    cache_scroll_->box(FL_DOWN_BOX);
    cache_scroll_->align(FL_ALIGN_TOP_LEFT | FL_ALIGN_INSIDE);
    cache_scroll_->type(Fl_Scroll::VERTICAL);
    cache_scroll_->begin();
    
    int cache_width = 200;
    int line_height = 25;
    int x_start = 10;
    int y_start = panel_y + 25;
    int spacing_x = 10;
    int spacing_y = 5;
    
    for (int cache_id = 0; cache_id < 4; cache_id++) {
        int x = x_start + cache_id * (cache_width + spacing_x);
        int y = y_start;
        
        char cache_label[32];
        snprintf(cache_label, sizeof(cache_label), "Cache %d", cache_id);
        Fl_Box* title = new Fl_Box(x, y, cache_width, 20, cache_label);
        title->box(FL_FLAT_BOX);
        title->labelfont(FL_HELVETICA_BOLD);
        y += 25;
        
        for (int set = 0; set < 8; set++) {
            for (int way = 0; way < 2; way++) {
                CacheLineWidget* widget = new CacheLineWidget(x, y, cache_width, line_height);
                cache_line_widgets_[cache_id].push_back(widget);
                y += line_height + spacing_y;
            }
        }
    }
    
    cache_scroll_->end();
}

void MESISimulatorGUI::createBusPanel() {
    int panel_x = window_->w() * 3 / 4 + 5;
    int panel_y = 280;
    int panel_width = window_->w() - panel_x - 5;
    int panel_height = (window_->h() - panel_y - 10) / 2;
    
    bus_panel_ = new Fl_Group(panel_x, panel_y, panel_width, panel_height, "Bus Activity Log");
    bus_panel_->box(FL_DOWN_BOX);
    bus_panel_->align(FL_ALIGN_TOP_LEFT | FL_ALIGN_INSIDE);
    bus_panel_->begin();
    
    bus_title_ = new Fl_Box(panel_x + 5, panel_y + 5, panel_width - 10, 20, "Interconnect Messages");
    bus_title_->labelfont(FL_HELVETICA_BOLD);
    bus_title_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    
    bus_log_ = new BusLogWidget(panel_x + 5, panel_y + 30, 
                                 panel_width - 10, panel_height - 35);
    
    bus_panel_->end();
}

void MESISimulatorGUI::createStatsPanel() {
    int panel_x = window_->w() * 3 / 4 + 5;
    int panel_y = 280 + (window_->h() - 280 - 10) / 2 + 5;
    int panel_width = window_->w() - panel_x - 5;
    int panel_height = window_->h() - panel_y - 5;
    
    stats_scroll_ = new Fl_Scroll(panel_x, panel_y, panel_width, panel_height, "Cache Statistics");
    stats_scroll_->box(FL_DOWN_BOX);
    stats_scroll_->align(FL_ALIGN_TOP_LEFT | FL_ALIGN_INSIDE);
    stats_scroll_->type(Fl_Scroll::VERTICAL);
    stats_scroll_->begin();
    
    int stats_width = panel_width - 20;
    int stats_height = 180;
    int x = panel_x + 5;
    int y = panel_y + 25;
    int spacing = 10;
    
    for (int i = 0; i < 4; i++) {
        char label[32];
        snprintf(label, sizeof(label), "Cache %d Stats", i);
        
        cache_stats_[i] = new CacheStatsWidget(x, y, stats_width, stats_height, label);
        y += stats_height + spacing;
    }
    
    stats_scroll_->end();
}

// ============================================================================
// Métodos show() y run()
// ============================================================================

void MESISimulatorGUI::show() {
    window_->show();
}

int MESISimulatorGUI::run() {
    return Fl::run();
}

// ============================================================================
// Método para actualizar la interfaz gráfica en cada paso
// ============================================================================

void MESISimulatorGUI::cb_update_display(void* data) {
    MESISimulatorGUI* gui = static_cast<MESISimulatorGUI*>(data);
    gui->updateDisplay();
    Fl::repeat_timeout(0.1, cb_update_display, data);
}


// ============================================================================
// Métodos de actualización de la interfaz
// ============================================================================

void MESISimulatorGUI::updateDisplay() {
    if (!system_loaded_) {
        return;
    }
    
    std::unique_lock<std::mutex> lock(display_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        return;
    }
    
    updatePEDisplays();
    updateCacheDisplays();
    updateBusLog();
    updateStatsDisplay();
    
    Fl::check();
}

void MESISimulatorGUI::updatePEDisplays() {
    for (int i = 0; i < 4; i++) {
        if (pes_[i]) {
            const uint64_t* regs = pes_[i]->getRegisters();
            int pc = static_cast<int>(pes_[i]->getPC());
            bool finished = pes_[i]->hasFinished();
            
            pe_widgets_[i]->setRegisters(regs, pc, finished);
        }
    }
}

void MESISimulatorGUI::updateCacheDisplays() {
    for (int cache_id = 0; cache_id < 4; cache_id++) {
        if (!caches_[cache_id]) continue;
        
        int widget_index = 0;
        
        for (uint32_t set = 0; set < 8; set++) {
            for (uint32_t way = 0; way < 2; way++) {
                try {
                    auto line_info = caches_[cache_id]->getLineInfo(set, way);
                    
                    int mesi_int = static_cast<int>(line_info.mesi);
                    
                    cache_line_widgets_[cache_id][widget_index]->setLineData(
                        set,
                        way,
                        line_info.tag,
                        line_info.valid,
                        line_info.dirty,
                        mesi_int,
                        line_info.last_use
                    );
                    
                    widget_index++;
                } catch (const std::exception& e) {
                    widget_index++;
                }
            }
        }
    }
}

void MESISimulatorGUI::updateBusLog() {
    std::lock_guard<std::mutex> lock(bus_log_mutex_);
    
    for (const auto& msg : bus_messages_) {
        bus_log_->addMessage(msg);
    }
    
    bus_messages_.clear();
}

void MESISimulatorGUI::updateStatsDisplay() {
    for (int i = 0; i < 4; i++) {
        if (caches_[i]) {
            auto stats = caches_[i]->getStats();
            
            cache_stats_[i]->setStats(
                stats.hits,
                stats.misses,
                stats.line_fills,
                stats.writebacks,
                stats.bus_rd,
                stats.bus_rdx,
                stats.bus_inv,
                stats.snoop_to_I,
                stats.snoop_to_S,
                stats.snoop_flush
            );
        }
    }
}

// ============================================================================
// Método auxiliar para logging de mensajes del bus
// ============================================================================

void MESISimulatorGUI::logBusMessage(const std::string& msg) {
    {
        std::lock_guard<std::mutex> lock(bus_log_mutex_);
        bus_messages_.push_back(msg);
    }
}

// ============================================================================
// Métodos auxiliares de simulación
// ============================================================================

void MESISimulatorGUI::stepAllPEs() {
    if (!system_loaded_) {
        return;
    }
    
    if (all_pes_finished_) {
        return;
    }
    
    int intentos = 0;
    const int max_intentos = 4;
    bool executed_something = false;
    
    while (intentos < max_intentos && !executed_something) {
        int pe_id = current_pe_for_step_;
        
        current_pe_for_step_ = (current_pe_for_step_ + 1) % 4;
        intentos++;
        
        if (!pe_alive_[pe_id]) {
            continue;
        }
        
        if (pes_[pe_id] && !pes_[pe_id]->hasFinished()) {
            try {
                pes_[pe_id]->executeNextInstruction();
                
                std::ostringstream oss;
                oss << "[Step " << (global_step_count_ + 1) << "] PE" << pe_id 
                    << " ejecutó instrucción (PC=" << pes_[pe_id]->getPC() << ")";
                logBusMessage(oss.str());
                
                global_step_count_++;
                executed_something = true;
                
                if (pes_[pe_id]->hasFinished()) {
                    std::ostringstream finish_oss;
                    finish_oss << "[PE" << pe_id << "] ha terminado su ejecución";
                    logBusMessage(finish_oss.str());
                    pe_alive_[pe_id] = false;
                }
                
            } catch (const std::exception& e) {
                std::ostringstream oss;
                oss << "[PE" << pe_id << "] ERROR: " << e.what();
                logBusMessage(oss.str());
                
                pe_alive_[pe_id] = false;
            }
        } else {
            if (pe_alive_[pe_id]) {
                std::ostringstream oss;
                oss << "[PE" << pe_id << "] ha terminado su ejecución";
                logBusMessage(oss.str());
                pe_alive_[pe_id] = false;
            }
        }
    }
    
    if (!executed_something) {
        bool all_finished = true;
        for (int i = 0; i < 4; i++) {
            if (pes_[i] && !pes_[i]->hasFinished()) {
                all_finished = false;
                break;
            }
        }
        
        if (all_finished) {
    all_pes_finished_ = true;
    logBusMessage("=== Todos los PEs han terminado su ejecución ===");
    
    // FLUSH TODAS LAS CACHÉS ANTES DE LEER RESULTADOS
    logBusMessage("=== Flushing all caches ===");
    for (int i = 0; i < 4; i++) {
        if (caches_[i]) {
            caches_[i]->flushAll();
        }
    }
    logBusMessage("All caches flushed.");
    
    // Ahora sí leer y mostrar resultados finales
    try {
        double partial_sums[4];
        double total = 0.0;
        uint64_t base_addr_partial = 0x0100;
        
        for (int i = 0; i < 4; i++) {
            partial_sums[i] = memoria_->readDouble(base_addr_partial + i * 64);
            total += partial_sums[i];
            
            std::ostringstream oss;
            oss << "PE" << i << " partial_sum = " << partial_sums[i];
            logBusMessage(oss.str());
        }
        
        std::ostringstream result_oss;
        result_oss << "FINAL RESULT: Dot Product = " << total;
        logBusMessage(result_oss.str());
        
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "ERROR reading results: " << e.what();
        logBusMessage(oss.str());
    }
    
    btn_step_->deactivate();
    btn_continue_->deactivate();
            
            status_box_->copy_label("All PEs have finished execution");
        }
    }
}

void MESISimulatorGUI::executeThreadedPE(int pe_id) {
    if (pe_id < 0 || pe_id >= 4 || !pes_[pe_id]) {
        return;
    }
    
    std::ostringstream oss;
    oss << "[PE" << pe_id << "] Thread started";
    logBusMessage(oss.str());
    
    while (!pes_[pe_id]->hasFinished() && running_) {
        try {
            pes_[pe_id]->executeNextInstruction();
        } catch (const std::exception& e) {
            std::ostringstream err_oss;
            err_oss << "[PE" << pe_id << "] ERROR: " << e.what();
            logBusMessage(err_oss.str());
            break;
        }
    }
    
    oss.str("");
    oss << "[PE" << pe_id << "] Thread finished";
    logBusMessage(oss.str());
}

void MESISimulatorGUI::resetSystem() {
    running_ = false;
    
    for (auto& t : pe_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    pe_threads_.clear();
    
    for (int i = 0; i < 4; i++) {
        if (pes_[i]) {
            pes_[i]->reset();
        }
        if (caches_[i]) {
            caches_[i]->invalidateAll();
            caches_[i]->resetStats();
        }
    }
    
    if (memoria_) {
        memoria_->resetStats();
    }
    
    system_loaded_ = false;
    global_step_count_ = 0;
    
    current_pe_for_step_ = 0;
    pe_alive_ = std::vector<bool>(4, true);
    all_pes_finished_ = false;
    
    {
        std::lock_guard<std::mutex> lock(bus_log_mutex_);
        bus_messages_.clear();
    }
    
    if (bus_log_) {
        bus_log_->clear();
    }
    
    btn_step_->deactivate();
    btn_continue_->deactivate();
    btn_run_all_->deactivate();
    
    status_box_->copy_label("System reset. Load system to begin.");
    
    updateDisplay();
}

void MESISimulatorGUI::exitProgram() {
    running_ = false;
    
    for (auto& t : pe_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    pe_threads_.clear();
    
    logBusMessage("=== Exiting program ===");
    status_box_->copy_label("Exiting... Goodbye!");
    Fl::check();
    
    if (window_) {
        window_->hide();
    }
    
    exit(0);
}

void MESISimulatorGUI::loadSystem() {
    if (system_loaded_) {
        resetSystem();
    }
    
    logBusMessage("=== Loading system ===");
    status_box_->copy_label("Loading system...");
    Fl::check();
    
    try {
        memoria_ = std::make_unique<MainMemory>();
        logBusMessage("Main memory created (512 x 64-bit words)");
        Fl::check();
        
        adapter_ = std::make_unique<MainMemoryAdapter>(*memoria_);
        logBusMessage("Memory adapter created");
        
        bus_ = std::make_unique<Interconnect>();
        logBusMessage("Interconnect (bus) created");
        Fl::check();
        
        for (int i = 0; i < 4; i++) {
            caches_[i] = std::make_unique<Cache2Way>(*adapter_);
            caches_[i]->setId(i);
            caches_[i]->setBus(bus_.get());
            
            caches_[i]->setLogCallback([this](const std::string& msg) {
                this->logBusMessage(msg);
            });
            
            bus_->attach(caches_[i].get());
            
            std::ostringstream oss;
            oss << "Cache " << i << " created and attached to bus";
            logBusMessage(oss.str());
        }
        Fl::check();
        
        for (int i = 0; i < 4; i++) {
            pes_[i] = std::make_unique<ProcessingElement>(i);
            pes_[i]->setCache(caches_[i].get());
            
            std::ostringstream oss;
            oss << "PE " << i << " created";
            logBusMessage(oss.str());
        }
        Fl::check();
        
        uint64_t base_addr_A = 0x0000;
        uint64_t base_addr_B = 0x0080;
        uint64_t base_addr_partial = 0x0100;
        
        for (int i = 0; i < vector_size_; i++) {
            memoria_->writeDouble(base_addr_A + i * 8, static_cast<double>(i + 1));
            memoria_->writeDouble(base_addr_B + i * 8, 2.0);
        }
        
        for (int i = 0; i < 4; i++) {
            memoria_->writeDouble(base_addr_partial + i * 64, 0.0);
        }
        
        uint64_t shared_counter = 0x0200;
        memoria_->writeDouble(shared_counter, 0.0);
        
        logBusMessage("Memory initialized with test vectors");
        logBusMessage("Vector A: [1.0, 2.0, 3.0, ..., 16.0]");
        logBusMessage("Vector B: [2.0, 2.0, 2.0, ..., 2.0]");
        logBusMessage("Expected dot product: 272.0");
        Fl::check();
        
        int elements_per_pe = vector_size_ / 4;

        for (int pe_id = 0; pe_id < 4; pe_id++) {
            std::vector<Instruction> program;
            
            uint64_t my_start_A = base_addr_A + pe_id * elements_per_pe * 8;
            uint64_t my_start_B = base_addr_B + pe_id * elements_per_pe * 8;
            uint64_t my_partial = base_addr_partial + pe_id * 64;
            
            pes_[pe_id]->setRegister(0, my_start_A);
            pes_[pe_id]->setRegister(1, my_start_B);
            pes_[pe_id]->setRegister(2, my_partial);
            pes_[pe_id]->setRegister(3, elements_per_pe);
            pes_[pe_id]->setRegisterDouble(4, 0.0);
            
            std::ostringstream init_oss;
            init_oss << "PE" << pe_id << " registers initialized: "
                     << "REG0=0x" << std::hex << my_start_A 
                     << " REG1=0x" << my_start_B
                     << " REG2=0x" << my_partial
                     << " REG3=" << std::dec << elements_per_pe;
            logBusMessage(init_oss.str());
            
            pes_[pe_id]->setRegister(7, shared_counter);
            program.push_back({InstructionType::LOAD, 7, 7, 0, 0});
            
            program.push_back({InstructionType::LOAD, 4, 2, 0, 0});
            
            int loop_start = program.size();
            
            program.push_back({InstructionType::LOAD, 5, 0, 0, 0});
            program.push_back({InstructionType::LOAD, 6, 1, 0, 0});
            program.push_back({InstructionType::FMUL, 7, 5, 6, 0});
            program.push_back({InstructionType::FADD, 4, 4, 7, 0});
            program.push_back({InstructionType::INC, 0, 0, 0, 0});
            program.push_back({InstructionType::INC, 1, 0, 0, 0});
            program.push_back({InstructionType::DEC, 3, 0, 0, 0});
            program.push_back({InstructionType::JNZ, 3, 0, 0, loop_start});
            
            program.push_back({InstructionType::STORE, 4, 2, 0, 0});
            
            pes_[pe_id]->loadProgram(program);
            
            std::ostringstream oss;
            oss << "PE " << pe_id << " program loaded (" << program.size() << " instructions)";
            logBusMessage(oss.str());
        }
        
        system_loaded_ = true;
        current_pe_for_step_ = 0;
        pe_alive_ = std::vector<bool>(4, true);
        all_pes_finished_ = false;
        btn_step_->activate();
        btn_continue_->activate();
        btn_run_all_->activate();
        
        status_box_->copy_label("System loaded successfully. Ready to execute.");
        logBusMessage("=== System ready ===");
        
        Fl::check();
        
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "ERROR loading system: " << e.what();
        logBusMessage(oss.str());
        status_box_->copy_label("ERROR: Failed to load system");
        system_loaded_ = false;
        Fl::check();
    }
}

// ============================================================================
// Callbacks de botones
// ============================================================================

void MESISimulatorGUI::cb_load_system(Fl_Widget* w, void* data) {
    MESISimulatorGUI* gui = static_cast<MESISimulatorGUI*>(data);
    gui->loadSystem();
}

void MESISimulatorGUI::cb_step(Fl_Widget* w, void* data) {
    MESISimulatorGUI* gui = static_cast<MESISimulatorGUI*>(data);
    gui->stepExecution();
}

void MESISimulatorGUI::cb_continue(Fl_Widget* w, void* data) {
    MESISimulatorGUI* gui = static_cast<MESISimulatorGUI*>(data);
    gui->continueExecution();
}

void MESISimulatorGUI::cb_run_all(Fl_Widget* w, void* data) {
    MESISimulatorGUI* gui = static_cast<MESISimulatorGUI*>(data);
    gui->runAll();
}

void MESISimulatorGUI::cb_reset(Fl_Widget* w, void* data) {
    MESISimulatorGUI* gui = static_cast<MESISimulatorGUI*>(data);
    gui->resetSystem();
}

void MESISimulatorGUI::cb_exit(Fl_Widget* w, void* data) {
    MESISimulatorGUI* gui = static_cast<MESISimulatorGUI*>(data);
    gui->exitProgram();
}

// ============================================================================
// Métodos de ejecución llamados por los callbacks
// ============================================================================

void MESISimulatorGUI::stepExecution() {
    if (!system_loaded_) {
        status_box_->copy_label("ERROR: System not loaded");
        return;
    }
    
    if (all_pes_finished_) {
        status_box_->copy_label("All PEs have finished execution");
        return;
    }
    
    stepping_ = true;
    stepAllPEs();
    stepping_ = false;
    
    if (!all_pes_finished_) {
        std::ostringstream oss;
        oss << "Step " << global_step_count_ << " completed";
        
        int activos = 0;
        for (int i = 0; i < 4; i++) {
            if (pes_[i] && !pes_[i]->hasFinished()) {
                activos++;
            }
        }
        oss << " (" << activos << " PEs activos)";
        
        status_box_->copy_label(oss.str().c_str());
    }
    
    updateDisplay();
}

void MESISimulatorGUI::continueExecution() {
    if (!system_loaded_) {
        status_box_->copy_label("ERROR: System not loaded");
        return;
    }
    
    if (all_pes_finished_) {
        status_box_->copy_label("All PEs have finished execution");
        return;
    }
    
    running_ = true;
    btn_step_->deactivate();
    btn_continue_->deactivate();
    btn_run_all_->deactivate();
    
    status_box_->copy_label("Executing (continue mode)...");
    Fl::check();
    
    while (running_) {
        stepAllPEs();
        
        bool all_finished = true;
        for (int i = 0; i < 4; i++) {
            if (pes_[i] && !pes_[i]->hasFinished()) {
                all_finished = false;
                break;
            }
        }
        
        if (all_finished) {
            running_ = false;
            status_box_->copy_label("Execution completed.");
            logBusMessage("=== All PEs finished execution ===");
            break;
        }
        
        if (breakpoint_interval_ > 0 && global_step_count_ % breakpoint_interval_ == 0) {
            running_ = false;
            std::ostringstream oss;
            oss << "Paused at breakpoint (step " << global_step_count_ << ")";
            status_box_->copy_label(oss.str().c_str());
            
            std::ostringstream log_oss;
            log_oss << "=== BREAKPOINT alcanzado en step " << global_step_count_ << " ===";
            logBusMessage(log_oss.str());
            break;
        }
        
        if (global_step_count_ % 10 == 0) {
            std::ostringstream oss;
            oss << "Executing... step " << global_step_count_;
            status_box_->copy_label(oss.str().c_str());
            updateDisplay();
            Fl::check();
        }
    }
    
    btn_step_->activate();
    btn_continue_->activate();
    btn_run_all_->activate();
    
    updateDisplay();
}

void MESISimulatorGUI::runAll() {
    if (!system_loaded_) {
        status_box_->copy_label("ERROR: System not loaded");
        return;
    }
    
    if (all_pes_finished_) {
        status_box_->copy_label("All PEs have finished execution");
        return;
    }
    
    running_ = true;
    btn_step_->deactivate();
    btn_continue_->deactivate();
    btn_run_all_->deactivate();
    btn_load_->deactivate();
    
    status_box_->copy_label("Executing in parallel (Run All mode)...");
    logBusMessage("=== Starting parallel execution ===");
    Fl::check();
    
    pe_threads_.clear();
    
    for (int i = 0; i < 4; i++) {
        pe_threads_.emplace_back(&MESISimulatorGUI::executeThreadedPE, this, i);
    }
    
    for (auto& t : pe_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    pe_threads_.clear();
    
    running_ = false;
    
    logBusMessage("=== Flushing all caches ===");
    for (int i = 0; i < 4; i++) {
        if (caches_[i]) {
            caches_[i]->flushAll();
        }
    }
    
    logBusMessage("=== Execution completed ===");
    logBusMessage("All caches flushed.");
    
    try {
        double partial_sums[4];
        double total = 0.0;
        uint64_t base_addr_partial = 0x0100;
        
        for (int i = 0; i < 4; i++) {
            partial_sums[i] = memoria_->readDouble(base_addr_partial + i * 64);
            total += partial_sums[i];
            
            std::ostringstream oss;
            oss << "PE" << i << " partial_sum = " << partial_sums[i];
            logBusMessage(oss.str());
        }
        
        std::ostringstream result_oss;
        result_oss << "FINAL RESULT: Dot Product = " << total;
        logBusMessage(result_oss.str());
        
        std::ostringstream status_oss;
        status_oss << "Execution completed. Result = " << total;
        status_box_->copy_label(status_oss.str().c_str());
        
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "ERROR reading results: " << e.what();
        logBusMessage(oss.str());
        status_box_->copy_label("Execution completed with errors.");
    }
    
    btn_step_->activate();
    btn_continue_->activate();
    btn_run_all_->activate();
    btn_load_->activate();
    
    updateDisplay();
}