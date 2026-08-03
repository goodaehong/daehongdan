#include "sensor_conversion.h"
#include <fstream>
#include <cmath>

constexpr float ADS1115_LSB = 6.144f / 32768.0f;
constexpr float SENSOR_VCC = 5.0f;
constexpr float RL_MQ2_KOHM = 0.98f;
constexpr float RL_MQ9_KOHM = 9.8f;
constexpr float MQ9_RO_KOHM = 5.28f;
constexpr float MQ2_RO_KOHM = 2.82f;

bool readDHT22(float& out_temp, float& out_hum) {
    std::ifstream tempFile("/sys/devices/platform/dht22/temp_value");
    std::ifstream humFile("/sys/devices/platform/dht22/humid_value");
    if (tempFile.is_open() && humFile.is_open()) {
        tempFile >> out_temp;
        humFile >> out_hum;
        return true;
    }
    return false;
}

bool readADS1115(int& raw_mq9, int& raw_mq2) {
    std::ifstream mq9f("/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/mq9_value");
    std::ifstream mq2f("/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/mq2_value");
    if (mq9f.is_open() && mq2f.is_open()) {
        mq9f >> raw_mq9;
        mq2f >> raw_mq2;
        return true;
    }
    return false;
}

static float rawToRsKohm(int raw, float rl_kohm) {
    float voltage = raw * ADS1115_LSB;
    if (voltage < 0.05f) voltage = 0.05f;
    return rl_kohm * (SENSOR_VCC - voltage) / voltage;
}

float mq9ToGasPpm(int raw) {
    float rs = rawToRsKohm(raw, RL_MQ9_KOHM);
    float ratio = rs / MQ9_RO_KOHM;
    if (ratio >= 9.0f) return 0.0f;
    return 504.8f * std::pow(ratio, -2.754f);
}

float mq2ToSmokePpm(int raw) {
    float rs = rawToRsKohm(raw, RL_MQ2_KOHM);
    float ratio = rs / MQ2_RO_KOHM;
    if (ratio >= 9.0f) return 0.0f;
    return 3220.0f * std::pow(ratio, -2.218f);
}