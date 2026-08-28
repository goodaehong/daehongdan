#pragma once
#include <opencv2/opencv.hpp>
#include <atomic>
#include <mutex>
#include <vector>

#include "evac_map_tools/EvacPlanner.h"   // FireCell
#include "net/link.h"
#include "sensors/sensor_reader.h"
#include "db/Database.h"

// 스레드 경계를 넘기는 상태만 여기 모은다.
// 입력 스레드(sensorWorker · cameraWorker)가 채우고, 제어 루프(controlLoop)가 읽는다.
// 판단·대응은 controlLoop 한 곳에서만 하므로 순서가 어긋날 일이 없다.

// 4채널 프레임 공유 저장소. 채널별 mutex로 워커/제어 스레드 간 경합 방지
struct FrameStore {
    cv::Mat frames[4];
    std::mutex mtx[4];
    std::atomic<long> lastFrameTs[4]{};   // 마지막 프레임 수신 시각 (visionOk 판정용)
    std::atomic<int> frameW[4]{};   // 원본 프레임 크기. Qt가 ROI 좌표 변환에 필요
    std::atomic<int> frameH[4]{};
};

// 채널별 최신 감지 상태. cameraWorker가 갱신, controlLoop이 읽음
struct DetectionState {
    std::atomic<bool> fire{false};
    std::atomic<bool> smoke{false};
    std::atomic<long> lastInferTs{0};     // 마지막 추론 결과 시각 (visionOk 판정용)

    // 이 채널에서 잡힌 화재들. 떨어진 불은 각각 경로를 막으므로 전부 보관한다
    // (개수가 변해서 atomic으로 못 다룸 — 잠금으로 처리)
    std::mutex fireMtx;
    std::vector<FireCell> fires;
};

// 최신 센서값. sensorWorker가 갱신, controlLoop이 읽음
// 읽기가 실패해도 값을 0으로 떨어뜨리지 않는다 — "가스 0ppm = 안전"으로
// 오판해 위험이 저절로 풀리기 때문. 대신 lastOkTs로 신선도만 알린다
struct SensorState {
    std::mutex mtx;
    SensorReading latest{};            // 마지막으로 정상 읽은 값
    std::atomic<long> lastOkTs{0};     // 그 값을 읽은 시각 (0 = 아직 한 번도 못 읽음)
};

extern DetectionState detState[4];
extern SensorState    sensorState;

extern Database g_db;   // 전역 DB. main에서 open

// 보정 계산 완료 알림은 계산 스레드에서 오는데, 콜백이 함수 포인터라
// link 를 넘겨받을 수 없다. main 에서 여기 채워두고 그걸 쓴다
extern Link* g_link;
