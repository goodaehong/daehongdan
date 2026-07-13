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
    cv::Mat makeFireColorMask(const cv::Mat& frame, const cv::Mat& hsv) const;
    cv::Mat makeForegroundMask(const cv::Mat& frame, const cv::Mat& gray);
    cv::Mat makeSkinMask(const cv::Mat& frame, const cv::Mat& hsv) const;

    void cleanupBinaryMask(cv::Mat& mask, int openSize, int closeSize, int dilateSize) const;

    double calculateIoU(const cv::Rect& a, const cv::Rect& b) const;
    bool isSameCandidate(const cv::Rect& previous, const cv::Rect& current) const;

private:
    cv::Ptr<cv::BackgroundSubtractorMOG2> mog2;

    cv::Mat prevGray;
    cv::Mat prevVal;
    cv::Mat prevFireMask;

    int frameIndex = 0;
    int fireConfirmCount = 0;
    int candidateMissCount = 0;
    int strongFireCount = 0;
    int weakKeepCount = 0;
    bool fireConfirmed = false;

    // 직전 화염 후보의 위치와 박스를 잠깐 유지한다.
    cv::Rect previousCandidateBox;
};