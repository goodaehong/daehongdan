#include "FireView.h"

#include <algorithm>
#include <cstdio>
#include <string>

using namespace cv;
using namespace std;

#if FIRE_ENABLE_GUI
namespace
{
    void drawDetectionResult(Mat& display, const DetectionResult& result)
    {
        for (const DetectionBox& detection : result.boxes)
        {
            const Scalar color = detection.type == DetectionType::FIRE
                ? Scalar(0, 0, 255)
                : Scalar(255, 255, 0);

            rectangle(display, detection.box, color, 2);
            putText(display, detection.label,
                Point(detection.box.x, max(20, detection.box.y - 8)),
                FONT_HERSHEY_SIMPLEX, 0.55, color, 2);
        }
    }
}
#endif

bool FireView::show(const Mat& frame, const FireRuntimeSnapshot& snapshot, double displayFps)
{
#if !FIRE_ENABLE_GUI
    (void)frame;
    (void)snapshot;
    (void)displayFps;
    return true;
#else
    if (frame.empty()) return processEvents(1);

    Mat displaySource = frame.clone();
    if (snapshot.boxIsFresh)
        drawDetectionResult(displaySource, snapshot.detection);

    Mat display;
    resize(displaySource, display, Size(960, 540), 0, 0, INTER_AREA);

    const bool candidateVisible = snapshot.resultIsFresh &&
        snapshot.resultAgeMs <= snapshot.boxFreshLimitMs &&
        (snapshot.detection.candidateDisplayReady ||
            (snapshot.alarm.rawFireTiming &&
                snapshot.alarm.pendingFireMs >= snapshot.alarm.requiredConfirmMs *
                (snapshot.alarm.ambiguousWarmObject ? 0.80 : 0.65)));

    const char* stateText = "NORMAL";
    Scalar stateColor(0, 255, 0);
    int thickness = 2;

    if (snapshot.alarm.alarmActive)
    {
        stateText = "FIRE DETECTED";
        stateColor = Scalar(0, 0, 255);
        thickness = 3;
    }
    else if (candidateVisible)
    {
        stateText = "FIRE CANDIDATE";
        stateColor = Scalar(0, 165, 255);
    }

    putText(display, stateText, Point(20, 40),
        FONT_HERSHEY_SIMPLEX, 1.0, stateColor, thickness);

    char performanceText[240];
    snprintf(performanceText, sizeof(performanceText),
        "Display %.1f FPS | Detect %.1f ms | Source %.0f ms | Done %.0f ms | Confirm %d | Evidence %.0f/%.0f ms | Raw %d/%d | Risk %s",
        displayFps, snapshot.averageDetectMs, snapshot.resultAgeMs, snapshot.completedAgeMs,
        snapshot.detection.confirmCount, snapshot.alarm.pendingFireMs, snapshot.alarm.requiredConfirmMs,
        snapshot.alarm.rawFireResultCount, snapshot.alarm.requiredRawFireResults,
        snapshot.alarm.ambiguousWarmObject ? "WARM" : "NORMAL");

    putText(display, performanceText, Point(20, 80),
        FONT_HERSHEY_SIMPLEX, 0.60, Scalar(255, 255, 255), 2);
    imshow("Fire Detection", display);

#if FIRE_DEBUG_VIEW
    if (snapshot.hasResult && snapshot.resultFrameId != 0 &&
        snapshot.resultFrameId != cachedDebugFrameId_)
    {
        const FireDebugImages& debug = snapshot.detection.debugImages;
        if (!debug.fireColorMask.empty() || !debug.skinMask.empty() ||
            !debug.foregroundMask.empty() || !debug.candidateMask.empty())
        {
            cachedDebugPanel_ = makeDebugPanel(debug);
            cachedDebugFrameId_ = snapshot.resultFrameId;
            imshow("Fire Debug Masks", cachedDebugPanel_);
        }
    }
#endif

    return processEvents(1);
#endif
}

bool FireView::processEvents(int delayMs) const
{
#if FIRE_ENABLE_GUI
    const char key = static_cast<char>(waitKey(delayMs));
    return key != 'q' && key != 27;
#else
    (void)delayMs;
    return true;
#endif
}

void FireView::close() const
{
#if FIRE_ENABLE_GUI
    destroyAllWindows();
#endif
}

#if FIRE_ENABLE_GUI && FIRE_DEBUG_VIEW
Mat FireView::makeDebugTile(const Mat& source, const string& title) const
{
    const Size tileSize(FIRE_DEBUG_TILE_WIDTH, FIRE_DEBUG_TILE_HEIGHT);
    Mat gray;

    if (source.empty())
    {
        gray = Mat::zeros(tileSize, CV_8UC1);
    }
    else
    {
        if (source.channels() == 1)
            gray = source;
        else
            cvtColor(source, gray, COLOR_BGR2GRAY);

        if (gray.size() != tileSize)
            resize(gray, gray, tileSize, 0, 0, INTER_NEAREST);
    }

    Mat tile;
    cvtColor(gray, tile, COLOR_GRAY2BGR);
    putText(tile, title, Point(12, 28),
        FONT_HERSHEY_SIMPLEX, 0.70, Scalar(0, 255, 255), 2);
    return tile;
}

Mat FireView::makeDebugPanel(const FireDebugImages& debug) const
{
    Mat top, bottom, panel;
    hconcat(makeDebugTile(debug.fireColorMask, "Fire color mask"),
        makeDebugTile(debug.skinMask, "Skin mask"), top);
    hconcat(makeDebugTile(debug.foregroundMask, "Foreground mask"),
        makeDebugTile(debug.candidateMask, "Candidate mask"), bottom);
    vconcat(top, bottom, panel);
    return panel;
}
#endif