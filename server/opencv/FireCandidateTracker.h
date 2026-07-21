#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

#include "DetectionTypes.h"

struct FireTrackingResult
{
    bool candidate = false;
    bool detected = false;
    bool candidateDisplayReady = false;

    int hitCount = 0;
    int confirmCount = 0;

    std::vector<DetectionBox> boxes;
};

//  ,   ,       .
//        .
class FireCandidateTracker
{
public:
    FireTrackingResult update(
        const std::vector<DetectionBox>& acceptedBoxes
    );

    void reset();

    //         .
    // FireDetector          .
    bool hasTrustedTrack() const;
    cv::Rect trackedBox() const;

private:
    double calculateIoU(
        const cv::Rect& a,
        const cv::Rect& b
    ) const;

    bool isSameCandidate(
        const cv::Rect& previous,
        const cv::Rect& current
    ) const;

private:
    int fireConfirmCount_ = 0;
    int candidateMissCount_ = 0;
    int strongFireCount_ = 0;
    int weakKeepCount_ = 0;
    int persistenceHoldCount_ = 0;
    bool fireConfirmed_ = false;

    cv::Rect previousCandidateBox_;
};