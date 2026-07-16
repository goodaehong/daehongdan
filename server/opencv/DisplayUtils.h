#pragma once

#include <opencv2/opencv.hpp>
#include "DetectionTypes.h"

void drawDetectionResult(cv::Mat& display, const DetectionResult& result);