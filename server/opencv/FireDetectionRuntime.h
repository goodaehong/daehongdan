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

// FireDetector  ,   ,  freshness,
// FireAlarmController    .
//  main Qt        .
class FireDetectionRuntime
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    FireDetectionRuntime();
    ~FireDetectionRuntime();

    FireDetectionRuntime(const FireDetectionRuntime&) = delete;
    FireDetectionRuntime& operator=(const FireDetectionRuntime&) = delete;

    //        .
    void submitFrame(
        const cv::Mat& frame,
        std::uint64_t frameId,
        TimePoint sourceTime = Clock::now()
    );

    //       Detector/Alarm   .
    void resetStream();

    // UI  .     Alarm  .
    FireRuntimeSnapshot poll(
        TimePoint now = Clock::now()
    );

    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};