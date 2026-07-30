#pragma once

#include <memory>
#include <string>

#include <opencv2/opencv.hpp>

#include "DetectionTypes.h"

class SmokeDetector
{
public:
    SmokeDetector();
    ~SmokeDetector();

    SmokeDetector(const SmokeDetector&) = delete;
    SmokeDetector& operator=(const SmokeDetector&) = delete;

    bool load(const std::string& paramPath, const std::string& binPath);
    SmokeDetectionResult detect(const cv::Mat& inputFrame);
    bool isReady() const;
    std::string lastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
