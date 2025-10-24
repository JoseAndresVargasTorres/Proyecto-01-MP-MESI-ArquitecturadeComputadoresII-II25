# Makefile para Proyecto I - Sistema Multiprocesador con MESI
# CE-4302 Arquitectura de Computadores II

# Compilador y flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -O2

# Flags de FLTK (obtenidos automáticamente)
FLTK_CXXFLAGS = $(shell fltk-config --cxxflags)
FLTK_LDFLAGS = $(shell fltk-config --ldflags)

# Directorios
SRC_DIR = .
BUILD_DIR = build

# Archivos fuente (Eliminado memory_adapter.cpp)
SOURCES = \
    $(SRC_DIR)/cache.cpp \
    $(SRC_DIR)/gui.cpp \
    $(SRC_DIR)/main_gui.cpp \
    $(SRC_DIR)/main_memory.cpp \
    $(SRC_DIR)/processing_element.cpp

# Archivos objeto
OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

# Ejecutable (en el directorio actual)
EXEC = mesi_simulator

# Regla por defecto
all: directories $(EXEC)

# Crear directorios necesarios
directories:
	@mkdir -p $(BUILD_DIR)

# ==========================================
# EJECUTABLE PRINCIPAL CON GUI
# ==========================================
$(EXEC): $(OBJECTS)
	@echo "========================================="
	@echo "Enlazando ejecutable final..."
	@echo "========================================="
	$(CXX) $(CXXFLAGS) $^ -o $@ $(FLTK_LDFLAGS)
	@echo ""
	@echo "✓✓✓ COMPILACIÓN EXITOSA ✓✓✓"
	@echo "Ejecutable creado: $(EXEC)"
	@echo ""
	@echo "Para ejecutar:"
	@echo "  make run"
	@echo "O directamente:"
	@echo "  ./$(EXEC)"
	@echo ""

# ==========================================
# COMPILACIÓN DE ARCHIVOS FUENTE
# ==========================================
$(BUILD_DIR)/cache.o: $(SRC_DIR)/cache.cpp $(SRC_DIR)/cache.hpp $(SRC_DIR)/interconnect.hpp
	@echo "[1/5] Compilando cache.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/gui.o: $(SRC_DIR)/gui.cpp $(SRC_DIR)/gui.hpp
	@echo "[2/5] Compilando gui.cpp..."
	$(CXX) $(CXXFLAGS) $(FLTK_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/main_gui.o: $(SRC_DIR)/main_gui.cpp $(SRC_DIR)/gui.hpp
	@echo "[3/5] Compilando main_gui.cpp..."
	$(CXX) $(CXXFLAGS) $(FLTK_CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/main_memory.o: $(SRC_DIR)/main_memory.cpp $(SRC_DIR)/main_memory.hpp
	@echo "[4/5] Compilando main_memory.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/processing_element.o: $(SRC_DIR)/processing_element.cpp $(SRC_DIR)/processing_element.hpp
	@echo "[5/5] Compilando processing_element.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ==========================================
# REGLAS DE LIMPIEZA
# ==========================================
clean:
	@echo "Limpiando archivos compilados..."
	rm -rf $(BUILD_DIR)
	rm -f $(EXEC)
	@echo "✓ Limpieza completada"

cleanobj:
	@echo "Limpiando solo objetos..."
	rm -rf $(BUILD_DIR)
	@echo "✓ Objetos eliminados"

# ==========================================
# EJECUCIÓN
# ==========================================
run: $(EXEC)
	@echo "========================================="
	@echo "Ejecutando simulador MESI..."
	@echo "========================================="
	@./$(EXEC)

# ==========================================
# AYUDA
# ==========================================
help:
	@echo "====================================="
	@echo "Makefile - Proyecto I CE-4302"
	@echo "Sistema Multiprocesador con MESI"
	@echo "====================================="
	@echo ""
	@echo "Targets disponibles:"
	@echo "  make          - Compila el simulador con GUI"
	@echo "  make run      - Compila y ejecuta el simulador"
	@echo "  make clean    - Elimina todos los archivos compilados"
	@echo "  make cleanobj - Elimina solo los archivos objeto"
	@echo "  make help     - Muestra esta ayuda"
	@echo ""
	@echo "Requisitos:"
	@echo "  - g++ con soporte C++17"
	@echo "  - FLTK 1.3 o superior"
	@echo "  - pthread"
	@echo ""
	@echo "Instalación de FLTK (Ubuntu/Debian):"
	@echo "  sudo apt-get install libfltk1.3-dev"
	@echo ""

# Targets independientes
.PHONY: all clean cleanobj run help directories
