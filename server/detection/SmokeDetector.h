#pragma once

// NCNN으로 내보낸 D-Fire YOLOv8n 모델을 로드해 smoke 클래스만 디코딩한다.
// 시간 누적과 채널 순회는 SmokeDetectionRuntime이 담당한다.


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
