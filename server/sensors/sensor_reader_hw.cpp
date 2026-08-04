#include "sensor_reader.h"
#include "sensor_conversion.h"
#include <fstream>

namespace {

    bool readDHT22(float& outTemp, float& outHum) {
        std::ifstream tempFile("/sys/devices/platform/dht22/temp_value");
        std::ifstream humFile("/sys/devices/platform/dht22/humid_value");
        if (!tempFile.is_open() || !humFile.is_open()) return false;
        tempFile >> outTemp;
        humFile >> outHum;
        return true;
    }

    bool readADS1115(int& rawMq9, int& rawMq2, int& rawFlame) {
        std::ifstream mq9f("/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/mq9_value");
        std::ifstream mq2f("/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/mq2_value");
        std::ifstream flamef("/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/flame_value");
        if (!mq9f.is_open() || !mq2f.is_open() || !flamef.is_open()) return false;
        mq9f >> rawMq9;
        mq2f >> rawMq2;
        flamef >> rawFlame;
        return true;
    }

} // namespace

bool SensorReader_Read(SensorReading& out) {
    float temp, hum;
    if (!readDHT22(temp, hum)) return false;

    int rawMq9, rawMq2, rawFlame;
    if (!readADS1115(rawMq9, rawMq2, rawFlame)) return false;

    out.temp = temp;
    out.humidity = hum;
    out.gasPpm = mq9ToGasPpm(rawMq9);
    out.smokePpm = mq2ToSmokePpm(rawMq2);
    out.flameVal = rawToVoltage(rawFlame);
    return true;
}