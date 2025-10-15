#include <iostream>
#include <iomanip>
#include "main_memory.hpp"
#include "processing_element.hpp"

using namespace std;

int main() {
    cout << "=== Prueba del Sistema MP ===" << endl << endl;
    
    // 1. Crear memoria principal
    cout << "1. Creando memoria principal (512 palabras de 64 bits)..." << endl;
    MainMemory memory;
    
    // 2. Cargar algunos valores double en memoria
    cout << "2. Cargando vectores A y B en memoria..." << endl;
    double vector_a[] = {1.5, 2.5, 3.5, 4.5};
    double vector_b[] = {2.0, 3.0, 4.0, 5.0};
    
    uint64_t addr_a = 0;    // Vector A empieza en dirección 0
    uint64_t addr_b = 32;   // Vector B empieza en dirección 32
    
    for (int i = 0; i < 4; i++) {
        memory.writeDouble(addr_a + i * 8, vector_a[i]);
        memory.writeDouble(addr_b + i * 8, vector_b[i]);
    }
    
    cout << "   Vector A: ";
    for (int i = 0; i < 4; i++) {
        cout << vector_a[i] << " ";
    }
    cout << endl;
    
    cout << "   Vector B: ";
    for (int i = 0; i < 4; i++) {
        cout << vector_b[i] << " ";
    }
    cout << endl << endl;
    
    // 3. Verificar lectura de memoria
    cout << "3. Verificando lectura desde memoria..." << endl;
    cout << "   A[0] desde memoria: " << memory.readDouble(addr_a) << endl;
    cout << "   B[2] desde memoria: " << memory.readDouble(addr_b + 16) << endl << endl;
    
    // 4. Crear un PE
    cout << "4. Creando Processing Element PE0..." << endl;
    ProcessingElement pe0(0);
    
    // 5. Configurar registros iniciales
    cout << "5. Configurando registros del PE0..." << endl;
    pe0.setRegisterDouble(5, 1.5);  // REG5 = 1.5
    pe0.setRegisterDouble(6, 2.0);  // REG6 = 2.0
    pe0.setRegister(3, 4);           // REG3 = 4 (contador)
    
    cout << "   REG5 = " << pe0.getRegisterDouble(5) << endl;
    cout << "   REG6 = " << pe0.getRegisterDouble(6) << endl;
    cout << "   REG3 = " << pe0.getRegister(3) << endl << endl;
    
    // 6. Crear un programa simple
    cout << "6. Cargando programa en PE0..." << endl;
    vector<Instruction> program;
    
    // FMUL REG7, REG5, REG6  ; REG7 = 1.5 * 2.0 = 3.0
    Instruction fmul;
    fmul.type = InstructionType::FMUL;
    fmul.reg_dest = 7;
    fmul.reg_src1 = 5;
    fmul.reg_src2 = 6;
    program.push_back(fmul);
    
    // FADD REG4, REG7, REG7  ; REG4 = 3.0 + 3.0 = 6.0
    Instruction fadd;
    fadd.type = InstructionType::FADD;
    fadd.reg_dest = 4;
    fadd.reg_src1 = 7;
    fadd.reg_src2 = 7;
    program.push_back(fadd);
    
    // DEC REG3               ; REG3 = 3
    Instruction dec;
    dec.type = InstructionType::DEC;
    dec.reg_dest = 3;
    program.push_back(dec);
    
    pe0.loadProgram(program);
    cout << "   Programa cargado (3 instrucciones)" << endl << endl;
    
    // 7. Ejecutar instrucciones
    cout << "7. Ejecutando programa..." << endl;
    int step = 0;
    while (!pe0.hasFinished() && step < 10) {
        cout << "   Paso " << step << ":" << endl;
        cout << "      Antes - REG3=" << pe0.getRegister(3) 
             << " REG4=" << fixed << setprecision(2) << pe0.getRegisterDouble(4)
             << " REG7=" << pe0.getRegisterDouble(7) << endl;
        
        pe0.executeNextInstruction();
        
        cout << "      Despues - REG3=" << pe0.getRegister(3)
             << " REG4=" << pe0.getRegisterDouble(4)
             << " REG7=" << pe0.getRegisterDouble(7) << endl;
        step++;
    }
    cout << endl;
    
    // 8. Mostrar resultados finales
    cout << "8. Resultados finales del PE0:" << endl;
    cout << "   REG4 (resultado): " << pe0.getRegisterDouble(4) << endl;
    cout << "   REG7 (intermedio): " << pe0.getRegisterDouble(7) << endl;
    cout << "   REG3 (contador): " << pe0.getRegister(3) << endl << endl;
    
    // 9. Mostrar estadísticas
    cout << "9. Estadisticas:" << endl;
    cout << "   Memoria - Lecturas: " << memory.getReadCount() 
         << ", Escrituras: " << memory.getWriteCount() << endl;
    cout << "   PE0 - Lecturas: " << pe0.getReadOps() 
         << ", Escrituras: " << pe0.getWriteOps() << endl << endl;
    
    cout << "=== Prueba completada exitosamente ===" << endl;
    
    return 0;
}

//Código de Compilación
//g++ -std=c++11 -pthread main.cpp main_memory.cpp processing_element.cpp -o test_mp
//./test_mp
