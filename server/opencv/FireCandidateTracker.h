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

// 점수와 박스 위치의 연속성을 이용해 화염 후보를 시간축으로 확인한다.
// 현재 주 실행 경로는 FlameDetector 내부 Track을 사용하므로 이 클래스는 호환용이다.
class FireCandidateTracker
{
public:
    FireTrackingResult update(
        const std::vector<DetectionBox>& acceptedBoxes
    );

    void reset();

    // 확정됐거나 지속 유지 중인 추적 영역이 있는지 반환한다.
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
