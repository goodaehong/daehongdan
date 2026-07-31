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

// 프레임 제출, 비동기 검출, 결과 freshness, 알람 유지까지 담당한다.
// main이나 Qt에서는 이 클래스 인터페이스만 사용하면 된다.
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

    void resetStream();
    FireRuntimeSnapshot poll(TimePoint now = Clock::now());
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};