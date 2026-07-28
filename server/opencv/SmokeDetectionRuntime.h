#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <opencv2/opencv.hpp>

#include "DetectionTypes.h"

struct SmokeRuntimeSnapshot
{
    SmokeDetectionResult detection;

    // Qt/server integration should send smokeDetected and smokeScore.
    bool smokeDetected = false;
    bool modelReady = false;
    bool hasResult = false;
    bool resultIsFresh = false;
    bool boxIsFresh = false;

    int positiveHits = 0;
    int consecutiveMisses = 0;
    std::uint64_t resultFrameId = 0;

    double smokeScore = 0.0;
    double detectMs = 0.0;
    double averageDetectMs = 0.0;
    double resultAgeMs = 0.0;
    double completedAgeMs = 0.0;
    std::string modelError;
};

// One NCNN model and one worker thread are shared by all camera channels.
// Each channel owns only its newest pending frame; old queued frames are dropped.
class SmokeDetectionRuntime
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit SmokeDetectionRuntime(
        std::size_t channelCount,
        const std::string& paramPath,
        const std::string& binPath
    );
    ~SmokeDetectionRuntime();

    SmokeDetectionRuntime(const SmokeDetectionRuntime&) = delete;
    SmokeDetectionRuntime& operator=(const SmokeDetectionRuntime&) = delete;

    bool submitFrame(
        std::size_t channelIndex,
        const cv::Mat& frame,
        std::uint64_t frameId,
        TimePoint sourceTime = Clock::now()
    );

    void resetChannel(std::size_t channelIndex);
    SmokeRuntimeSnapshot poll(
        std::size_t channelIndex,
        TimePoint now = Clock::now()
    );
    bool isModelReady() const;
    std::string modelError() const;
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
