#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

enum class DetectionType { FIRE, SMOKE };

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

    bool strongFireEvidence = false;
    bool tinyCandidate = false;
    bool skinLikeCandidate = false;
    bool coreHaloEvidence = false;
    bool reflectionLikeCandidate = false;
    bool brightBackgroundEvidence = false;
    bool fingerLikeCandidate = false;
    bool skinSeparatedFlameEvidence = false;
    bool requiresExtendedConfirmation = false;

    // The candidate survived through the color-only path inside an already trusted ROI.
    // This is used only to keep a previously confirmed stationary flame alive.
    bool trackedPersistenceEvidence = false;

    double brightnessDiffMean = 0.0;
    double maskChangeRatio = 0.0;
    double redOrangeRatio = 0.0;
};

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
