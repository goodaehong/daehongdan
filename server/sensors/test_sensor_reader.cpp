#include "sensor_reader.h"
#include <iostream>

int main() {
    SensorReading r;
    if (!SensorReader_Read(r)) {
        std::cerr << "읽기 실패\n";
        return 1;
    }
    std::cout << "temp=" << r.temp
              << " humidity=" << r.humidity
              << " gasPpm=" << r.gasPpm
              << " smokePpm=" << r.smokePpm
              << " flameVal(V)=" << r.flameVal << "\n";
    return 0;
}