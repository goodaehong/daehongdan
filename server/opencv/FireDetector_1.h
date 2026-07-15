#pragma once

#include <opencv2/opencv.hpp>

#include "DetectionTypes.h"
#include "FireCandidateTracker.h"
#include "FireMaskBuilder.h"

//      .
//   FireMaskBuilder,    FireCandidateTracker .
class FireDetector
{
public:
    FireDetector();

    DetectionResult detect(const cv::Mat& inputFrame);
    void reset();

private:
    FireMaskBuilder maskBuilder_;
    FireCandidateTracker candidateTracker_;

    cv::Mat prevVal;
    cv::Mat prevFireMask;

    int frameIndex = 0;
};
