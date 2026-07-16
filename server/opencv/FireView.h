#pragma once

#include <cstdint>
#include <string>

#include <opencv2/opencv.hpp>

#include "AppConfig.h"
#include "FireDetectionRuntime.h"

// /OpenCV   .
// Qt     FireDetectionRuntime    .
class FireView
{
public:
    bool show(
        const cv::Mat& frame,
        const FireRuntimeSnapshot& snapshot,
        double displayFps
    );

    bool processEvents(int delayMs = 1) const;
    void close() const;

private:
#if FIRE_DEBUG_VIEW
    cv::Mat makeDebugTile(
        const cv::Mat& source,
        const std::string& title
    ) const;

    cv::Mat makeDebugPanel(
        const FireDebugImages& debug
    ) const;

    cv::Mat cachedDebugPanel_;
    std::uint64_t cachedDebugFrameId_ = 0;
#endif
};
