#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "AppConfig.h"
#include "FireDetectionRuntime.h"
#include "SmokeDetectionRuntime.h"

struct FireViewChannel
{
    cv::Mat frame;
    FireRuntimeSnapshot fire;
    SmokeRuntimeSnapshot smoke;
    double displayFps = 0.0;
    std::string title;
};

class FireView
{
public:
    explicit FireView(const std::string& windowName = "Fire/Smoke Detection");

    bool show(
        const cv::Mat& frame,
        const FireRuntimeSnapshot& fireSnapshot,
        const SmokeRuntimeSnapshot& smokeSnapshot,
        double displayFps
    );

    bool showGrid(const std::vector<FireViewChannel>& channels);

    bool processEvents(int delayMs = 1) const;
    void close();

private:
    std::string windowName_;
    std::string debugWindowName_;

#if FIRE_ENABLE_GUI
    bool windowCreated_ = false;
#if FIRE_DEBUG_VIEW
    bool debugWindowCreated_ = false;
#endif

    cv::Mat makeChannelTile(
        const cv::Mat& frame,
        const FireRuntimeSnapshot& fireSnapshot,
        const SmokeRuntimeSnapshot& smokeSnapshot,
        double displayFps,
        const std::string& title
    ) const;
#endif

#if FIRE_ENABLE_GUI && FIRE_DEBUG_VIEW
    cv::Mat makeDebugTile(const cv::Mat& source, const std::string& title) const;
    cv::Mat makeDebugPanel(const FireDebugImages& debug) const;

    cv::Mat cachedDebugPanel_;
    std::uint64_t cachedDebugFrameId_ = 0;
#endif
};
