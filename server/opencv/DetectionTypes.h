#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

enum class DetectionType
{
    FIRE,
    SMOKE
};

struct FireDebugImages
{
    cv::Mat fireColorMask;
    cv::Mat skinMask;
    cv::Mat foregroundMask;
    cv::Mat candidateMask;
};

struct DetectionBox
{
    cv::Rect box;
    std::string label;
    DetectionType type = DetectionType::FIRE;
    double score = 0.0;

    // 최종 점수와 별개로, 실제 화염의 색상 층·밝기 변화가 있는지 표시한다.
    // 움직이는 노란 물체가 점수만 높게 나와 화재로 확정되는 것을 막는 데 사용한다.
    bool strongFireEvidence = false;
    double yellowDominantRatio = 0.0;

    // 작은 후보의 비율값이 1~2픽셀 노이즈로 과대평가되는 것을 막기 위한 정보
    bool tinyCandidate = false;
    bool skinLikeCandidate = false;
    bool coreHaloEvidence = false;
    double candidateSkinRatio = 0.0;
    double boxAreaPixels = 0.0;
    int firePixelCount = 0;
    int pureRedPixelCount = 0;
    int whiteCorePixelCount = 0;
};

struct DetectionResult
{
    bool candidate = false;
    bool detected = false;
    bool flicker = false;
    double area = 0.0;

    int hitCount = 0;
    int confirmCount = 0;

    // 원시 후보 존재 여부와 별개로, 거의 확정 직전일 때만 UI에 후보 문구를 표시한다.
    bool candidateDisplayReady = false;

    std::vector<DetectionBox> boxes;

    // 디버그용 중간 마스크. FIRE_DEBUG_VIEW가 1일 때만 채워진다.
    FireDebugImages debugImages;
};