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
        // sysfs read가 커널 드라이버 쪽에서 에러(체크섬 불일치 등)를 리턴하면
        // 스트림 추출이 조용히 실패하면서 outTemp/outHum이 초기화 안 된 채로 남는다.
        // 이 상태를 성공으로 착각하면 0도/쓰레기값이 정상값처럼 Qt까지 흘러간다 -> 반드시 확인.
        if (tempFile.fail() || humFile.fail()) return false;
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
        if (mq9f.fail() || mq2f.fail() || flamef.fail()) return false; // 같은 이유로 동일하게 방어
        return true;
    }

} // namespace

bool SensorReader_Read(SensorReading& out) {
    // DHT22는 ADS1115보다 체크섬 오류가 잦은데, 실패했다고 가스/연기/불꽃까지
    // 같이 죽일 이유가 없다. ADS1115(위험 판단에 직결) 성공 여부만 반환값을 결정하고,
    // DHT22는 실패 시 마지막 정상값을 그대로 쓴다 (0으로 떨어뜨리면 온습도만 왜곡되고
    // 판단 로직엔 안 쓰이니 무해하지만, 그래도 값 자체는 유지).
    static float lastTemp = 0.0f, lastHum = 0.0f;
    float temp, hum;
    bool dhtOk = readDHT22(temp, hum);
    if (dhtOk) {
        lastTemp = temp;
        lastHum = hum;
    }
    out.temp = lastTemp;
    out.humidity = lastHum;
    out.dhtOk = dhtOk;

    int rawMq9, rawMq2, rawFlame;
    if (!readADS1115(rawMq9, rawMq2, rawFlame)) return false;

    out.gasPpm = mq9ToGasPpm(rawMq9);
    out.smokePpm = mq2ToSmokePpm(rawMq2);
    out.flameVal = rawToVoltage(rawFlame);
    return true;
}