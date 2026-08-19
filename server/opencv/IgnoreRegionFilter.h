#pragma once

#include <cstddef>
#include <vector>

#include <opencv2/core.hpp>

#include "DetectionTypes.h"

// Qt에서 전달하는 0~1 정규화 다각형이다. enabled=false인 영역은 보관하되
// 감지 결과 필터에는 사용하지 않는다.
struct IgnoreRegion
{
    bool enabled = true;
    std::vector<cv::Point2f> points;
};

struct IgnoreRegionConfig
{
    std::vector<IgnoreRegion> regions;
    double overlapThreshold = 0.5;
};

// 원본 영상은 변경하지 않고, 검출 박스와 사용자 지정 Ignore ROI의 겹침만
// 계산한다. 등록된 활성 영역이 없으면 모든 검출을 그대로 통과시킨다.
class IgnoreRegionFilter
{
public:
    void setConfig(const IgnoreRegionConfig& config);
    void clear();

    bool enabled() const;
    double overlapThreshold() const;

    bool shouldIgnore(
        const cv::Rect& detectionBox,
        const cv::Size& frameSize) const;

    // 제외된 박스 수를 반환한다.
    std::size_t filter(
        std::vector<DetectionBox>& detections,
        const cv::Size& frameSize) const;

private:
    void rebuildMask(const cv::Size& frameSize) const;

    IgnoreRegionConfig config_;
    mutable cv::Mat ignoreMask_;
    mutable cv::Size maskSize_;
    mutable bool maskValid_ = false;
};
