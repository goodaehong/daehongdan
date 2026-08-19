#pragma once

// 센서 읽기 결과. 보정식 적용 끝난 물리 단위만 (판단은 judgement.h)
struct SensorReading {
    float temp;        // ℃ (DHT22)
    float humidity;    // % (DHT22)
    float gasPpm;      // ppm (MQ-9)
    float smokePpm;    // ppm (MQ-2)
    float flameVal;    // 불꽃 세기 (DFR0076 AO)
    bool  dhtOk;       // 이번 틱에 DHT22를 실제로 읽었는지 (false=캐시값 재사용, 최초 실행 전이면 0/0)
};

// 센서 1회 읽기. 실패 시 false (0 채우면 "가스 0ppm=안전"으로 오판함)
// 스레드 만들지 말 것 — server_main이 1초마다 부름
bool SensorReader_Read(SensorReading& out);