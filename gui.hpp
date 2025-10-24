// gui.hpp
// Interfaz gráfica para el simulador de sistema multiprocesador con protocolo MESI
// CE-4302 Arquitectura de Computadores II

#pragma once

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Group.H>
#include <FL/fl_draw.H>

#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <cstdint>

// Forward declarations
class Cache2Way;
class ProcessingElement;
class Interconnect;
class MainMemory;
class MainMemoryAdapter;
struct Instruction;

// ============================================================================
// CacheLineWidget - Widget para mostrar una línea de caché
// ============================================================================
class CacheLineWidget : public Fl_Box {
public:
    // Colores para estados MESI
    static constexpr Fl_Color BG_MODIFIED = FL_RED;
    static constexpr Fl_Color BG_EXCLUSIVE = FL_BLUE;
    static constexpr Fl_Color BG_SHARED = FL_GREEN;
    static constexpr Fl_Color BG_INVALID = FL_GRAY;

    CacheLineWidget(int x, int y, int w, int h, const char* label = nullptr);
    
    void setLineData(int set, int way, uint64_t tag, bool valid, 
                     bool dirty, int mesi_state, uint64_t lru);

protected:
    void draw() override;

private:
    int set_;
    int way_;
    uint64_t tag_;
    bool valid_;
    bool dirty_;
    int mesi_state_;  // 0=I, 1=S, 2=E, 3=M
    uint64_t lru_;
};

// ============================================================================
// RegisterWidget - Widget para mostrar registros de un PE
// ============================================================================
class RegisterWidget : public Fl_Box {
public:
    RegisterWidget(int x, int y, int w, int h, const char* label = nullptr);
    
    void setRegisters(const uint64_t* regs, int pc, bool finished);

protected:
    void draw() override;

private:
    uint64_t registers_[8];
    int pc_;
    bool finished_;
};

// ============================================================================
// BusLogWidget - Widget para el log del bus
// ============================================================================
class BusLogWidget : public Fl_Text_Display {
public:
    static constexpr int MAX_LINES = 1000;

    BusLogWidget(int x, int y, int w, int h, const char* label = nullptr);
    
    void addMessage(const std::string& msg);
    void clear();

private:
    Fl_Text_Buffer* buffer_;
};

// ============================================================================
// CacheStatsWidget - Widget para estadísticas de caché
// ============================================================================
class CacheStatsWidget : public Fl_Box {
public:
    CacheStatsWidget(int x, int y, int w, int h, const char* label = nullptr);
    
    void setStats(uint64_t hits, uint64_t misses, uint64_t line_fills,
                  uint64_t writebacks, uint64_t bus_rd, uint64_t bus_rdx,
                  uint64_t bus_inv, uint64_t snoop_i, uint64_t snoop_s,
                  uint64_t snoop_flush);

protected:
    void draw() override;

private:
    uint64_t hits_;
    uint64_t misses_;
    uint64_t line_fills_;
    uint64_t writebacks_;
    uint64_t bus_rd_;
    uint64_t bus_rdx_;
    uint64_t bus_inv_;
    uint64_t snoop_i_;
    uint64_t snoop_s_;
    uint64_t snoop_flush_;
};

// ============================================================================
// MESISimulatorGUI - Clase principal de la interfaz gráfica
// ============================================================================
class MESISimulatorGUI {
public:
    MESISimulatorGUI(int width = 1600, int height = 1000);
    ~MESISimulatorGUI();
    
    void show();
    int run();

    // Métodos de simulación
    void stepExecution();
    void continueExecution();
    void runAll();
    void resetSystem();
    void exitProgram();
    void loadSystem();

    // Actualización de la interfaz
    void updateDisplay();
    void updatePEDisplays();
    void updateCacheDisplays();
    void updateBusLog();
    void updateStatsDisplay();
    void updateMemoryStats();

    // Callbacks de los botones
    static void cb_load_system(Fl_Widget* w, void* data);
    static void cb_step(Fl_Widget* w, void* data);
    static void cb_continue(Fl_Widget* w, void* data);
    static void cb_run_all(Fl_Widget* w, void* data);
    static void cb_reset(Fl_Widget* w, void* data);
    static void cb_exit(Fl_Widget* w, void* data);

    // Callbacks para actualización periódica
    static void cb_update_display(void* data);
    
private:
    // Variables de control
    Fl_Window* window_;
    Fl_Group* control_panel_;
    Fl_Button* btn_load_;
    Fl_Button* btn_step_;
    Fl_Button* btn_continue_;
    Fl_Button* btn_run_all_;
    Fl_Button* btn_reset_;
    Fl_Button* btn_exit_;
    Fl_Box* status_box_;

    // Paneles de la GUI
    Fl_Group* pe_panel_;
    Fl_Scroll* cache_scroll_;
    Fl_Group* bus_panel_;
    Fl_Scroll* stats_scroll_;
    CacheStatsWidget* cache_stats_[4];
    BusLogWidget* bus_log_;
    Fl_Box* bus_title_;
    RegisterWidget* pe_widgets_[4];
    std::vector<CacheLineWidget*> cache_line_widgets_[4];
    Fl_Box* memory_stats_box_;

    // Variables de simulación
    std::unique_ptr<ProcessingElement> pes_[4];
    std::unique_ptr<Cache2Way> caches_[4];
    std::unique_ptr<Interconnect> bus_;
    std::unique_ptr<MainMemoryAdapter> adapter_;
    std::unique_ptr<MainMemory> memoria_;

    // Threads para ejecución paralela
    std::vector<std::thread> pe_threads_;

    // Métodos de configuración
    void createControlPanel();
    void createPEPanel();
    void createCachePanel();
    void createBusPanel();
    void createStatsPanel();

    // Funciones de simulación
    void logBusMessage(const std::string& msg);
    void stepAllPEs();
    void executeThreadedPE(int pe_id);

    std::atomic<bool> running_;
    std::atomic<bool> stepping_;
    int global_step_count_;
    int vector_size_;
    int breakpoint_interval_;
    bool system_loaded_;
    
    // Variables para stepping round-robin
    int current_pe_for_step_;
    std::vector<bool> pe_alive_;
    bool all_pes_finished_;

    // Mutexes
    std::mutex display_mutex_;
    std::mutex bus_log_mutex_;
    
    // Para almacenar mensajes del bus
    std::vector<std::string> bus_messages_;
};