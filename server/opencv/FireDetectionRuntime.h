#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include <opencv2/opencv.hpp>

#include "DetectionTypes.h"
#include "FireAlarmController.h"

struct FireRuntimeSnapshot
{
    DetectionResult detection;
    FireAlarmStatus alarm;

    bool hasResult = false;
    bool resultIsFresh = false;
    bool boxIsFresh = false;

    std::uint64_t resultFrameId = 0;

    double detectMs = 0.0;
    double averageDetectMs = 0.0;
    double resultAgeMs = 0.0;
    double completedAgeMs = 0.0;
    double resultFreshLimitMs = 0.0;
    double boxFreshLimitMs = 0.0;
};

// 채널 하나의 프레임 제출, 비동기 화염 검출, 결과 유효시간과 알람을 관리한다.
// 제출된 프레임은 누적하지 않고 최신 프레임으로 교체한다.
class FireDetectionRuntime
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    FireDetectionRuntime();
    ~FireDetectionRuntime();

    FireDetectionRuntime(const FireDetectionRuntime&) = delete;
    FireDetectionRuntime& operator=(const FireDetectionRuntime&) = delete;

    void submitFrame(
        const cv::Mat& frame,
        std::uint64_t frameId,
        TimePoint sourceTime = Clock::now()
    );

    // 카메라 재연결 또는 동영상 반복 시 이전 배경·추적·알람 상태를 폐기한다.
    void resetStream();
    FireRuntimeSnapshot poll(TimePoint now = Clock::now());
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
