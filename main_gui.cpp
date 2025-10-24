// main_gui.cpp
// Punto de entrada principal para el simulador MESI
// CE-4302 Arquitectura de Computadores II

#include "gui.hpp"

int main(int argc, char** argv) {
    // Crear la interfaz gráfica
    MESISimulatorGUI gui(1600, 1000);
    
    // Mostrar la ventana
    gui.show();
    
    // Ejecutar el loop de eventos de FLTK
    return gui.run();
}
