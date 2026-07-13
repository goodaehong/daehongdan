#pragma once

#include <opencv2/opencv.hpp>
#include "DetectionTypes.h"

class FireDetector
{
public:
    FireDetector();

    DetectionResult detect(const cv::Mat& inputFrame);
    void reset();

private:
    cv::Mat makeFireColorMask(const cv::Mat& frame);
    cv::Mat makeMotionMask(const cv::Mat& frame);
    cv::Mat makeSkinMask(const cv::Mat& frame);

    void cleanupCandidateMask(cv::Mat& mask);
    void densityDenoiseAndFill(cv::Mat& mask, int ksize, int denoiseThreshold, int fillThreshold);

private:
    cv::Mat prevGray;
    cv::Ptr<cv::BackgroundSubtractorMOG2> mog2;

    int frameIndex = 0;
    int fireConfirmCount = 0;
};
