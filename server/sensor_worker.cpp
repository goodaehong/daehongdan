#include "sensor_worker.h"

#include <chrono>
#include <ctime>
#include <iostream>
#include <thread>

#include "shared_state.h"
#include "sensors/sensor_reader.h"

void sensorWorker()
{
    // 센서 상태 로그는 변화 시에만 (실패 시 1초마다 도배 방지)
    bool prevOk = true;

    while (true) {
        SensorReading s;
        if (SensorReader_Read(s)) {
            if (!prevOk) std::cout << "[센서] 복구됨\n";
            prevOk = true;

            // 정상 값만 갱신한다. 실패 시 0으로 덮으면 "가스 0ppm = 안전"이 되어
            // 위험이 저절로 풀린다 — 값은 그대로 두고 신선도로만 알린다
            {
                std::lock_guard<std::mutex> lk(sensorState.mtx);
                sensorState.latest = s;
            }
            sensorState.lastOkTs = (long)std::time(nullptr);
        } else {
            if (prevOk) std::cerr << "[센서] 읽기 실패\n";
            prevOk = false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
