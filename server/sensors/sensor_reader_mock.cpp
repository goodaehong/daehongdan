#include "sensor_reader.h"
#include <random>

// mock 센서. 부품 오면 sensor_reader_hw.cpp로 교체 (CMake 옵션)
bool SensorReader_Read(SensorReading& out) {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    static int tick = 0;
    ++tick;

    // 평상시 기준값 + 흔들림
    out.temp     = 26.0f  + jitter(rng) * 2.0f;
    out.humidity = 45.0f  + jitter(rng) * 5.0f;
    out.gasPpm   = 45.0f  + jitter(rng) * 10.0f;
    out.smokePpm = 8.0f   + jitter(rng) * 3.0f;
    out.flameVal = 0.1f + jitter(rng) * 0.05f;   // 평상시 ~0.1V, 임계 1.0V 미만
    out.dhtOk = true;   // mock은 항상 정상

    // 데모 스파이크: 60초 주기 중 45~60초 구간은 가스 급상승 (Qt 경고 UI 테스트용)
    if (tick % 60 >= 45) {
        // 2500.0f 등으로 올려주어 Danger(2000 이상) 구간에 진입하도록 수정
        out.gasPpm   = 2500.0f + jitter(rng) * 100.0f; 
        out.smokePpm = 180.0f + jitter(rng) * 20.0f; // 연기는 150이 임계값이니 180이면 충분함
    }

    // 데모 스파이크2: 불꽃센서s (가스와 다른 타이밍 = 20~35초 구간)
    if (tick % 60 >= 20 && tick % 60 < 35) {
        out.flameVal = 2.5f + jitter(rng) * 1.0f;   // 임계 1.0V 초과 → 불꽃 감지
    }

    return true;   // mock은 항상 성공
}