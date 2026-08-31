#pragma once

// 연기 감지 파이프라인. 모델 추론 결과에 움직임 확인과 시간 누적을 더해
// 최종 연기 판정을 만든다. 모델 검출만으로는 오탐이 남기 때문이다.


#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <opencv2/opencv.hpp>

#include "DetectionTypes.h"
#include "IgnoreRegionFilter.h"

struct SmokeRuntimeSnapshot
{
    SmokeDetectionResult detection;

    // Qt/서버 연동 시 최종 연기 여부와 점수는 이 두 값을 사용한다.
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
    double pipelineLatencyMs = 0.0;
    std::string modelError;
};

// NCNN 모델 하나와 작업 스레드 하나를 모든 채널이 공유한다.
// 채널마다 최신 대기 프레임 하나만 유지하고 라운드로빈 방식으로 순차 추론한다.
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

    // 스레드 안전. 채널별 UI 설정이 비어 있으면 기존 연기 감지와 동일하다.
    bool setIgnoreRegionConfig(
        std::size_t channelIndex,
        const IgnoreRegionConfig& config);

    // 해당 채널의 프레임 이력, 움직임 이력, hits와 알람을 모두 초기화한다.
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
