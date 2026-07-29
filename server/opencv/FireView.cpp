#include "FireView.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace cv;
using namespace std;

#if FIRE_ENABLE_GUI
namespace
{
    constexpr int GRID_TILE_WIDTH = 640;
    constexpr int GRID_TILE_HEIGHT = 360;

    void drawDetectionResult(Mat& display, const vector<DetectionBox>& boxes)
    {
        for (const DetectionBox& detection : boxes)
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

    void drawPersonBoxes(Mat& display, const PersonMetadataFrame& metadata)
    {
        const Scalar color(0, 255, 0);
        for (const PersonDetection& person : metadata.persons)
        {
            rectangle(display, person.box, color, 2);
            char label[96];
            if (person.objectId.empty())
                snprintf(label, sizeof(label), "PERSON %.2f", person.confidence);
            else
                snprintf(label, sizeof(label), "PERSON %s %.2f",
                    person.objectId.c_str(), person.confidence);
            putText(display, label,
                Point(person.box.x, max(20, person.box.y - 8)),
                FONT_HERSHEY_SIMPLEX, 0.55, color, 2);
        }
    }

    void drawOutlinedText(
        Mat& image,
        const string& text,
        const Point& origin,
        double scale,
        const Scalar& color,
        int thickness)
    {
        putText(image, text, origin, FONT_HERSHEY_SIMPLEX,
            scale, Scalar(0, 0, 0), thickness + 3, LINE_AA);
        putText(image, text, origin, FONT_HERSHEY_SIMPLEX,
            scale, color, thickness, LINE_AA);
    }
}
#endif

FireView::FireView(const string& windowName)
    : windowName_(windowName), debugWindowName_(windowName + " - Fire Debug Masks")
{
}

#if FIRE_ENABLE_GUI
Mat FireView::makeChannelTile(
    const Mat& frame,
    const FireRuntimeSnapshot& fireSnapshot,
    const SmokeRuntimeSnapshot& smokeSnapshot,
    const PersonMetadataFrame& personMetadata,
    double displayFps,
    const string& title) const
{
    Mat tile;
    if (frame.empty())
    {
        tile = Mat::zeros(Size(GRID_TILE_WIDTH, GRID_TILE_HEIGHT), CV_8UC3);
        drawOutlinedText(tile, title, Point(16, 30), 0.75, Scalar(255, 255, 255), 2);
        drawOutlinedText(tile, "NO SIGNAL", Point(220, 190), 1.0, Scalar(0, 0, 255), 2);
        rectangle(tile, Rect(0, 0, tile.cols, tile.rows), Scalar(90, 90, 90), 2);
        return tile;
    }

    Mat displaySource = frame.clone();
    if (fireSnapshot.boxIsFresh)
        drawDetectionResult(displaySource, fireSnapshot.detection.boxes);
    if (smokeSnapshot.boxIsFresh)
        drawDetectionResult(displaySource, smokeSnapshot.detection.boxes);
    drawPersonBoxes(displaySource, personMetadata);

    resize(displaySource, tile, Size(GRID_TILE_WIDTH, GRID_TILE_HEIGHT), 0, 0, INTER_AREA);

    const bool fireCandidateVisible = fireSnapshot.resultIsFresh &&
        fireSnapshot.resultAgeMs <= fireSnapshot.boxFreshLimitMs &&
        (fireSnapshot.detection.candidateDisplayReady ||
            (fireSnapshot.alarm.rawFireTiming &&
                fireSnapshot.alarm.pendingFireMs >= fireSnapshot.alarm.requiredConfirmMs *
                (fireSnapshot.alarm.ambiguousWarmObject ? 0.80 : 0.65)));
    const bool smokeCandidateVisible = smokeSnapshot.resultIsFresh &&
        smokeSnapshot.detection.candidate;

    const char* stateText = "NORMAL";
    Scalar stateColor(0, 255, 0);
    int stateThickness = 2;
    if (fireSnapshot.alarm.alarmActive && smokeSnapshot.smokeDetected)
    {
        stateText = "FIRE + SMOKE DETECTED";
        stateColor = Scalar(0, 0, 255);
        stateThickness = 3;
    }
    else if (fireSnapshot.alarm.alarmActive)
    {
        stateText = "FIRE DETECTED";
        stateColor = Scalar(0, 0, 255);
        stateThickness = 3;
    }
    else if (smokeSnapshot.smokeDetected)
    {
        stateText = "SMOKE DETECTED";
        stateColor = Scalar(255, 255, 0);
        stateThickness = 3;
    }
    else if (fireCandidateVisible)
    {
        stateText = "FIRE CANDIDATE";
        stateColor = Scalar(0, 165, 255);
    }
    else if (smokeCandidateVisible)
    {
        stateText = "SMOKE CANDIDATE";
        stateColor = Scalar(255, 255, 0);
    }

    drawOutlinedText(tile, title, Point(16, 28), 0.70, Scalar(255, 255, 255), 2);
    drawOutlinedText(tile, stateText, Point(16, 60), 0.72, stateColor, stateThickness);

    char performanceText[240];
    snprintf(performanceText, sizeof(performanceText),
        "FPS %.1f | Fire %.1fms | Smoke %.1fms | score %.3f | hits %d",
        displayFps, fireSnapshot.averageDetectMs, smokeSnapshot.averageDetectMs,
        smokeSnapshot.smokeScore, smokeSnapshot.positiveHits);
    drawOutlinedText(tile, performanceText, Point(16, 88),
        0.46, Scalar(255, 255, 255), 1);
    char personText[120];
    snprintf(personText, sizeof(personText),
        "WiseAI %s | persons %zu",
        personMetadata.streamConnected ? "ON" : "OFF",
        personMetadata.persons.size());
    drawOutlinedText(tile, personText, Point(16, 110),
        0.44,
        personMetadata.streamConnected ? Scalar(0, 255, 0) : Scalar(150, 150, 150),
        1);
    rectangle(tile, Rect(0, 0, tile.cols, tile.rows), Scalar(90, 90, 90), 2);
    return tile;
}
#endif

bool FireView::show(
    const Mat& frame,
    const FireRuntimeSnapshot& fireSnapshot,
    const SmokeRuntimeSnapshot& smokeSnapshot,
    double displayFps)
{
#if !FIRE_ENABLE_GUI
    (void)frame;
    (void)fireSnapshot;
    (void)smokeSnapshot;
    (void)displayFps;
    return true;
#else
    imshow(windowName_, makeChannelTile(
        frame, fireSnapshot, smokeSnapshot, PersonMetadataFrame{},
        displayFps, windowName_));
    windowCreated_ = true;

#if FIRE_DEBUG_VIEW
    if (fireSnapshot.hasResult && fireSnapshot.resultFrameId != 0 &&
        fireSnapshot.resultFrameId != cachedDebugFrameId_)
    {
        const FireDebugImages& debug = fireSnapshot.detection.debugImages;
        if (!debug.fireColorMask.empty() || !debug.skinMask.empty() ||
            !debug.foregroundMask.empty() || !debug.candidateMask.empty())
        {
            cachedDebugPanel_ = makeDebugPanel(debug);
            cachedDebugFrameId_ = fireSnapshot.resultFrameId;
            imshow(debugWindowName_, cachedDebugPanel_);
            debugWindowCreated_ = true;
        }
    }
#endif
    return processEvents(1);
#endif
}

bool FireView::showGrid(const vector<FireViewChannel>& channels)
{
#if !FIRE_ENABLE_GUI
    (void)channels;
    return true;
#else
    vector<Mat> tiles;
    tiles.reserve(4);
    for (size_t index = 0; index < 4; ++index)
    {
        if (index < channels.size())
        {
            const FireViewChannel& channel = channels[index];
            tiles.push_back(makeChannelTile(
                channel.frame,
                channel.fire,
                channel.smoke,
                channel.person,
                channel.displayFps,
                channel.title));
        }
        else
        {
            FireRuntimeSnapshot fire;
            SmokeRuntimeSnapshot smoke;
            PersonMetadataFrame person;
            tiles.push_back(makeChannelTile(Mat(), fire, smoke, person, 0.0,
                "CH" + to_string(index + 1)));
        }
    }

    Mat top, bottom, grid;
    hconcat(tiles[0], tiles[1], top);
    hconcat(tiles[2], tiles[3], bottom);
    vconcat(top, bottom, grid);
    imshow(windowName_, grid);
    windowCreated_ = true;
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

void FireView::close()
{
#if FIRE_ENABLE_GUI
    if (windowCreated_)
    {
        try { destroyWindow(windowName_); }
        catch (const cv::Exception&) {}
        windowCreated_ = false;
    }
#if FIRE_DEBUG_VIEW
    if (debugWindowCreated_)
    {
        try { destroyWindow(debugWindowName_); }
        catch (const cv::Exception&) {}
        debugWindowCreated_ = false;
    }
#endif
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
