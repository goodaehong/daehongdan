#include "sensor_reader.h"
#include "sensor_conversion.h"

// 실센서. 유나님 sensor_conversion의 읽기·환산 함수를 SensorReading에 담기만 함
bool SensorReader_Read(SensorReading& out) {
    // 온습도 — 부가 정보라 실패해도 이전 값 유지하고 계속 진행
    static float lastTemp = 0.0f, lastHum = 0.0f;
    if (readDHT22(out.temp, out.humidity)) {
        lastTemp = out.temp;
        lastHum  = out.humidity;
    } else {
        out.temp     = lastTemp;
        out.humidity = lastHum;
    }

    // 가스·연기 — 판단의 핵심 입력. 실패하면 이번 주기는 판단 자체를 건너뜀
    int rawMq9 = 0, rawMq2 = 0;
    if (!readADS1115(rawMq9, rawMq2)) return false;   // 로그는 호출부에서

    out.gasPpm   = mq9ToGasPpm(rawMq9);
    out.smokePpm = mq2ToSmokePpm(rawMq2);

    out.flameVal = 0.0f;   // 불꽃센서(DFR0076) 아직 없음 — 유나님 추가 대기
    return true;
}