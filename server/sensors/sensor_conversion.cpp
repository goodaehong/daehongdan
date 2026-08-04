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

float mq9ToGasPpm(int raw) {
    float rs = rawToRsKohm(raw, RL_MQ9_KOHM);
    float ratio = rs / MQ9_RO_KOHM;
    if (ratio >= 2.0f) return 0.0f;   // LPG 곡선: 그래프 유효범위(200ppm) 미만은 0 처리
    return 924.5f * std::pow(ratio, -2.208f);   // LPG 곡선 기준 재계산 (2026-08-04, MQ-9 데이터시트 Fig.1)
}

float mq2ToSmokePpm(int raw) {
    float rs = rawToRsKohm(raw, RL_MQ2_KOHM);
    float ratio = rs / MQ2_RO_KOHM;
    if (ratio >= 9.0f) return 0.0f;
    return 3220.0f * std::pow(ratio, -2.218f);
}

float rawToVoltage(int raw) {
    return raw * ADS1115_LSB;
}