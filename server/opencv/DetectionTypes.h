#pragma once

#include <opencv2/opencv.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

enum class DetectionType { FIRE, SMOKE };

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

// 화염과 연기 검출기가 화면 표시 및 후속 판정에 공통으로 전달하는 박스다.
struct DetectionBox
{
    cv::Rect box;
    std::string label;
    DetectionType type = DetectionType::FIRE;
    double score = 0.0;

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
