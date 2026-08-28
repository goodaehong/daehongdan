#pragma once

#include <opencv2/opencv.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

enum class DetectionType { FIRE, SMOKE };
enum class FireConfirmReason { NONE, REGULAR, TINY, DYNAMIC_COLOR };
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
    cv::Mat analysisFrame;
    cv::Mat grayImage;
    cv::Mat hsvImage;
    cv::Mat yCrCbImage;
    cv::Mat fireColorMask;
    cv::Mat skinMask;
    cv::Mat foregroundMask;
    cv::Mat combinedMask;
    cv::Mat candidateMask;
    cv::Mat contourOverlay;
    cv::Mat featureScoreOverlay;
    cv::Mat trackingOverlay;
    cv::Mat confirmedOverlay;
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
    // FlameDetector 내부에서는 개별 트랙 ID이고, FireDetectionRuntime 최종
    // 결과에서는 가까운 화재를 묶은 안정적인 그룹 ID다.
    int trackId = -1;
    // 화재는 가까운 내부 트랙들을 하나의 그룹으로 묶어 외부에 전달한다.
    // 1보다 크면 여러 화재 트랙이 하나의 표시·분석 그룹에 포함된 것이다.
    int groupedTrackCount = 1;
    double normalizedX = 0.0;
    double normalizedY = 0.0;
    double normalizedWidth = 0.0;
    double normalizedHeight = 0.0;

    // 현재 요구사항에서는 화재 위치만 평면도/60x60 디스플레이에 사용한다.
    // 화재는 박스 아래쪽 중앙점을 대표 위치로 사용하고 연기는 false로 둔다.
    bool representativePositionValid = false;
    double representativeNormalizedX = 0.0;
    double representativeNormalizedY = 0.0;
    // 최근 실제 검출 위치를 짧게 평균한 값이다. 박스 흔들림 때문에 고정
    // 디스플레이의 안내 위치가 자주 바뀌는 것을 줄일 때 사용한다.
    bool smoothedRepresentativePositionValid = false;
    double smoothedRepresentativeNormalizedX = 0.0;
    double smoothedRepresentativeNormalizedY = 0.0;

    // Final zero-based 60x60 floor-map position calculated inside the OpenCV
    // runtime. Valid fire cells are 0..59 on both axes.
    // Consumers only serialize/use this result; they must not recalculate it.
    bool gridPositionValid = false;
    int gridX = -1;
    int gridY = -1;
    bool factoryPositionValid = false;
    double factoryXMetres = 0.0;
    double factoryYMetres = 0.0;

    // Ground footprint estimated from the detected fire box base.  A single
    // camera cannot recover flame volume, so the mapped base width is treated
    // as the diameter of a circular floor footprint.  The model name is
    // serialized with the value so consumers do not mistake it for a directly
    // measured burn area.
    bool factoryFootprintValid = false;
    double footprintLeftFactoryXMetres = 0.0;
    double footprintLeftFactoryYMetres = 0.0;
    double footprintRightFactoryXMetres = 0.0;
    double footprintRightFactoryYMetres = 0.0;
    double estimatedGroundWidthMetres = 0.0;
    double estimatedGroundAreaSquareMetres = 0.0;
    int footprintLeftGridX = -1;
    int footprintLeftGridY = -1;
    int footprintRightGridX = -1;
    int footprintRightGridY = -1;
    // At least one cell makes a small fire visible as a 3x3 display region.
    int displayRadiusCells = 0;
    std::vector<TrackPositionSample> positionHistory;

    // 화재 박스의 바닥 접촉 구간이다. Homography에는 세로로 솟은 박스의
    // 상단이 아니라 이 구간의 좌/중/우 점을 사용하는 것이 안전하다.
    bool fireFootprintValid = false;
    double footprintLeftNormalizedX = 0.0;
    double footprintRightNormalizedX = 0.0;
    double footprintNormalizedY = 0.0;
    bool strongFireEvidence = false;
    bool tinyCandidate = false;
    bool skinLikeCandidate = false;
    bool coreHaloEvidence = false;
    bool tinyFlameEvidence = false;
    bool dynamicColorFlameEvidence = false;
    bool reflectionLikeCandidate = false;
    bool stableFlameAnchor = false;
    int dynamicColorHistoryHits = 0;
    int dynamicColorHistorySize = 0;
    FireConfirmReason confirmReason = FireConfirmReason::NONE;
    int flameStructureHits = 0;
    int reflectionHits = 0;
    bool brightBackgroundEvidence = false;
    bool fingerLikeCandidate = false;
    bool skinSeparatedFlameEvidence = false;
    bool requiresExtendedConfirmation = false;

    // 이미 신뢰한 ROI에서 색상만으로 살아남은 정지 화염 유지용 후보다.
    bool trackedPersistenceEvidence = false;

    double brightnessDiffMean = 0.0;
    double maskChangeRatio = 0.0;
    double colorRatio = 0.0;
    double redOrangeRatio = 0.0;
    double whiteCoreRatio = 0.0;
    double tinyFlameRatio = 0.0;
    double skinRatio = 0.0;
    double motionRatio = 0.0;
    double valueStdDev = 0.0;
    double candidateBrightness = 0.0;
    double surroundingBrightness = -1.0;
    double brightnessRatio = 1.0;
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
