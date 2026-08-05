#pragma once

#include <opencv2/opencv.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

enum class DetectionType { FIRE, SMOKE };
enum class DetectionAreaTrend { UNKNOWN, SHRINKING, STABLE, GROWING };
enum class CameraHealthIssue { NONE, BLUR, OBSTRUCTION };

// 카메라 WiseAI 메타데이터를 원본 영상 픽셀 좌표로 변환한 사람 객체다.
struct PersonDetection
{
    cv::Rect box;
    double confidence = 0.0;
    std::string objectId;
};

struct PersonMetadataFrame
{
    std::vector<PersonDetection> persons;
    bool receiverRunning = false;
    bool streamConnected = false;
    double ageMs = std::numeric_limits<double>::infinity();
    std::uint64_t bytesReceived = 0;
    std::string status;
};

// FIRE_DEBUG_VIEW에서 화염 검출 단계별 마스크를 확인할 때 사용한다.
struct FireDebugImages
{
    cv::Mat fireColorMask;
    cv::Mat skinMask;
    cv::Mat foregroundMask;
    cv::Mat candidateMask;
};

// 동일 검출 트랙의 대표 위치를 시간순으로 전달한다. timestampMs는
// steady_clock 기반 단조 증가 시간이므로 절대 시각이 아니라 이동량/방향 계산에 사용한다.
struct TrackPositionSample
{
    std::uint64_t frameId = 0;
    std::int64_t timestampMs = 0;
    double normalizedX = 0.0;
    double normalizedY = 0.0;
};

// Warning-only camera image diagnosis. It never suppresses detections or creates ROI settings.
struct CameraHealthStatus
{
    bool valid = false;
    bool contaminationSuspected = false;
    CameraHealthIssue issue = CameraHealthIssue::NONE;
    double laplacianVariance = 0.0;
    double darkPixelRatio = 0.0;
    double brightPixelRatio = 0.0;
    int consecutiveSuspectFrames = 0;
};

// 화염과 연기 검출기가 화면 표시 및 후속 판정에 공통으로 전달하는 박스다.
struct DetectionBox
{
    cv::Rect box;
    std::string label;
    DetectionType type = DetectionType::FIRE;
    double score = 0.0;

    // 픽셀 box는 기존 화면 표시와의 호환을 위해 유지하고, 외부 연동용 좌표를
    // 원본 프레임 기준 0~1 범위로 함께 제공한다.
    int trackId = -1;
    double normalizedX = 0.0;
    double normalizedY = 0.0;
    double normalizedWidth = 0.0;
    double normalizedHeight = 0.0;

    // 현재 요구사항에서는 화재 위치만 평면도/64x64 디스플레이에 사용한다.
    // 화재는 박스 아래쪽 중앙점을 대표 위치로 사용하고 연기는 false로 둔다.
    bool representativePositionValid = false;
    double representativeNormalizedX = 0.0;
    double representativeNormalizedY = 0.0;
    std::vector<TrackPositionSample> positionHistory;

    // Image-space motion estimate. Speed is normalized image distance per second, not m/s.
    // Valid becomes true only after smoothed displacement and direction-consistency checks.
    bool imageMotionValid = false;
    double imageDirectionX = 0.0;
    double imageDirectionY = 0.0;
    double imageSpeedNormalizedPerSecond = 0.0;
    std::int64_t imageMotionWindowMs = 0;

    // 화재 트랙이 확정되기 전 최초 후보로 생성됐던 위치와 시각이다.
    bool firstSeenPositionValid = false;
    std::uint64_t firstSeenFrameId = 0;
    std::int64_t firstSeenTimestampMs = 0;
    double firstSeenNormalizedX = 0.0;
    double firstSeenNormalizedY = 0.0;

    // 최근 박스 면적 변화로 계산한 영상 기반 증가/감소 추세다.
    DetectionAreaTrend areaTrend = DetectionAreaTrend::UNKNOWN;
    double areaChangeRatio = 0.0;

    bool strongFireEvidence = false;
    bool tinyCandidate = false;
    bool skinLikeCandidate = false;
    bool coreHaloEvidence = false;
    bool reflectionLikeCandidate = false;
    bool brightBackgroundEvidence = false;
    bool fingerLikeCandidate = false;
    bool skinSeparatedFlameEvidence = false;
    bool requiresExtendedConfirmation = false;

    // 이미 신뢰한 ROI에서 색상만으로 살아남은 정지 화염 유지용 후보다.
    bool trackedPersistenceEvidence = false;

    double brightnessDiffMean = 0.0;
    double maskChangeRatio = 0.0;
    double redOrangeRatio = 0.0;
};

// FlameDetector의 한 번의 분석 결과이며 최종 알람 상태와는 구분된다.
struct DetectionResult
{
    bool candidate = false;
    bool detected = false;
    bool flicker = false;
    bool candidateDisplayReady = false;

    double area = 0.0;
    int hitCount = 0;
    int confirmCount = 0;

    std::vector<DetectionBox> boxes;
    FireDebugImages debugImages;
};

// SmokeDetector의 NCNN 1회 추론 결과다.
struct SmokeDetectionResult
{
    bool modelReady = false;
    bool candidate = false;
    double maxScore = 0.0;
    std::vector<DetectionBox> boxes;
    std::string error;
};
