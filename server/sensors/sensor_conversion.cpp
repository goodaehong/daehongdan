// sensor_conversion.h 구현. ADC 원시값을 ppm · 전압으로 환산한다.
// 환산식은 각 소자 데이터시트의 특성 곡선을 근거로 한다.

#include "sensor_conversion.h"
#include <cmath>

constexpr float ADS1115_LSB = 6.144f / 32768.0f;
constexpr float SENSOR_VCC = 5.0f;
constexpr float RL_MQ2_KOHM = 0.98f;
constexpr float RL_MQ9_KOHM = 9.8f;
constexpr float MQ9_RO_KOHM = 5.28f;
constexpr float MQ2_RO_KOHM = 2.82f;

static float rawToRsKohm(int raw, float rl_kohm) {
    float voltage = raw * ADS1115_LSB;
    if (voltage < 0.05f) voltage = 0.05f;
    return rl_kohm * (SENSOR_VCC - voltage) / voltage;
}

static float calculatePpm(float ratio, float coefficient, float exponent) {
    if (!std::isfinite(ratio) || ratio <= 0.0f) {
        return 0.0f;
    }

    const float ppm = coefficient * std::pow(ratio, exponent);
    return std::isfinite(ppm) ? std::max(0.0f, ppm) : 0.0f;
}

float mq9ToGasPpm(int raw) {
    float rs = rawToRsKohm(raw, RL_MQ9_KOHM);
    float ratio = rs / MQ9_RO_KOHM;
    return calculatePpm(ratio, 924.5f, -2.208f);
}

float mq2ToSmokePpm(int raw) {
    float rs = rawToRsKohm(raw, RL_MQ2_KOHM);
    float ratio = rs / MQ2_RO_KOHM;
    return calculatePpm(ratio, 3220.0f, -2.218f);
}

float rawToVoltage(int raw) {
    return raw * ADS1115_LSB;
}