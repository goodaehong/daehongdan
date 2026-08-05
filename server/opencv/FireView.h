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
    PersonMetadataFrame person;
    double displayFps = 0.0;
    std::string title;
};

// 화염·연기·WiseAI 결과를 채널 타일 또는 고정 2x2 화면으로 표시한다.
// FIRE_ENABLE_GUI=0인 Raspberry Pi 빌드에서는 표시 함수가 no-op으로 동작한다.
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

    // q 또는 Esc 입력 시 false를 반환한다.
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
        const PersonMetadataFrame& personMetadata,
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
