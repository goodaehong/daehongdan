#pragma once

// 화재 감지 파이프라인. 프레임 제출 → 검출 → 좌표 변환 → 결과 보관까지를
// 워커 스레드에서 처리한다. 호출하는 쪽은 제출과 조회만 하면 된다.


#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <opencv2/opencv.hpp>

#include "DetectionTypes.h"
#include "FireAlarmController.h"
#include "GridCoordinateMapper.h"
#include "IgnoreRegionFilter.h"

struct FireRuntimeSnapshot
{
    DetectionResult detection;
    FireAlarmStatus alarm;
    CameraHealthStatus cameraHealth;
    ArucoMappingStatus arucoMapping;

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

    // 스레드 안전. 빈 regions 또는 모두 disabled이면 기존 검출을 그대로 사용한다.
    void setIgnoreRegionConfig(const IgnoreRegionConfig& config);

    // 설치 보정과 동일한 ArUco 보드 형상을 읽는다. 운영 중에는 이 설정과
    // 카메라 내부 보정값에 일치하는 고정 Homography를 함께 로드한다.
    bool loadArucoBoardConfiguration(
        const std::string& configurationPath,
        std::size_t channelIndex);
    std::string arucoMappingError() const;
    bool loadCameraCalibration(
        const std::string& calibrationPath,
        std::size_t channelIndex);
    bool loadStaticHomography(
        const std::string& homographyPath,
        std::size_t channelIndex);
    std::string cameraCalibrationError() const;

    // 카메라 재연결 또는 동영상 반복 시 이전 배경·추적·알람 상태를 폐기한다.
    void resetStream();
    FireRuntimeSnapshot poll(TimePoint now = Clock::now());
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
