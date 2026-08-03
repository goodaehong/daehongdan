#include "sensor_reader.h"
#include <random>

// mock 센서. 부품 오면 sensor_reader_hw.cpp로 교체 (CMake 옵션)
bool SensorReader_Read(SensorReading& out) {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    static int tick = 0;
    ++tick;

    // 평상시 기준값 + 흔들림 (실센서 클린에어 실측에 맞춤: 가스 0 / 연기 27)
    out.temp     = 26.0f  + jitter(rng) * 2.0f;
    out.humidity = 45.0f  + jitter(rng) * 5.0f;
    out.gasPpm   = 1.0f   + jitter(rng) * 1.0f;    // 실측 0.0 근처
    out.smokePpm = 27.0f  + jitter(rng) * 2.0f;    // 실측 27 근처
    out.flameVal = 100.0f + jitter(rng) * 30.0f;   // raw AO(아날로그 출력), 환산 없음

    // 데모 스파이크: 45~60초 구간 가스 급상승 (임계값 실측 나오면 같이 조정)
    if (tick % 60 >= 45) {
        out.gasPpm   = 80.0f  + jitter(rng) * 10.0f;
        out.smokePpm = 150.0f + jitter(rng) * 20.0f;
    }

    // 데모 스파이크2: 불꽃센서 (가스와 다른 타이밍 = 20~35초 구간)
    if (tick % 60 >= 20 && tick % 60 < 35) {
        out.flameVal = 600.0f + jitter(rng) * 50.0f;   // 임계 400 초과 → 불꽃 감지
    }

    return true;   // mock은 항상 성공
}