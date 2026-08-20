#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>

#include "AppConfig.h"
#include "FireDetectionRuntime.h"
#include "IgnoreRegionFilter.h"
#include "SmokeDetectionRuntime.h"

namespace
{
    constexpr const char* WINDOW_NAME = "Detection Debug Runner";
    constexpr int MAX_DISPLAY_WIDTH = 1280;
    constexpr int MAX_DISPLAY_HEIGHT = 720;

    struct RoiEditor
    {
        std::vector<IgnoreRegion> committedRegions;
        std::vector<cv::Point2f> draftPoints;
        cv::Size displaySize;
    };

    // Live cameras must be drained continuously. The UI consumes only the newest
    // frame so a slow detector or display cannot build up seconds of latency.
    class LatestFrameReader
    {
    public:
        explicit LatestFrameReader(cv::VideoCapture& capture)
            : capture_(capture), thread_(&LatestFrameReader::readLoop, this)
        {
        }

        ~LatestFrameReader()
        {
            stop();
        }

        bool getLatest(cv::Mat& frame, std::uint64_t& sequence)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (latestFrame_.empty() || sequence == latestSequence_)
                return false;
            latestFrame_.copyTo(frame);
            sequence = latestSequence_;
            return true;
        }

        bool ended() const
        {
            return ended_.load();
        }

        void stop()
        {
            bool expected = true;
            if (!running_.compare_exchange_strong(expected, false))
                return;
            if (thread_.joinable())
                thread_.join();
        }

    private:
        void readLoop()
        {
            while (running_.load())
            {
                cv::Mat frame;
                if (!capture_.read(frame) || frame.empty())
                {
                    ended_ = true;
                    break;
                }

                std::lock_guard<std::mutex> lock(mutex_);
                latestFrame_ = std::move(frame);
                ++latestSequence_;
            }
        }

        cv::VideoCapture& capture_;
        std::atomic<bool> running_{ true };
        std::atomic<bool> ended_{ false };
        std::thread thread_;
        std::mutex mutex_;
        cv::Mat latestFrame_;
        std::uint64_t latestSequence_ = 0;
    };

    const char* fireConfirmReasonName(FireConfirmReason reason)
    {
        switch (reason)
        {
        case FireConfirmReason::REGULAR: return "REGULAR";
        case FireConfirmReason::TINY: return "TINY";
        case FireConfirmReason::DYNAMIC_COLOR: return "DYNAMIC_COLOR";
        default: return "NONE";
        }
    }

    const char* cameraIssueName(CameraHealthIssue issue)
    {
        switch (issue)
        {
        case CameraHealthIssue::BLUR: return "BLUR";
        case CameraHealthIssue::OBSTRUCTION: return "OBSTRUCTION";
        default: return "NONE";
        }
    }

    cv::Point normalizedPoint(double x, double y, const cv::Size& size)
    {
        return cv::Point(
            std::clamp(static_cast<int>(std::lround(x * (size.width - 1))),
                0, std::max(0, size.width - 1)),
            std::clamp(static_cast<int>(std::lround(y * (size.height - 1))),
                0, std::max(0, size.height - 1)));
    }

    void drawOutlinedText(
        cv::Mat& image,
        const std::string& text,
        const cv::Point& origin,
        double scale,
        const cv::Scalar& color)
    {
        cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX,
            scale, cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
        cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX,
            scale, color, 1, cv::LINE_AA);
    }

    void onMouse(int event, int x, int y, int, void* userData)
    {
        auto* editor = static_cast<RoiEditor*>(userData);
        if (!editor || editor->displaySize.width <= 0 || editor->displaySize.height <= 0)
            return;

        if (event == cv::EVENT_LBUTTONDOWN)
        {
            editor->draftPoints.emplace_back(
                std::clamp(static_cast<float>(x) / editor->displaySize.width, 0.0F, 1.0F),
                std::clamp(static_cast<float>(y) / editor->displaySize.height, 0.0F, 1.0F));
        }
        else if (event == cv::EVENT_RBUTTONDOWN && !editor->draftPoints.empty())
        {
            editor->draftPoints.pop_back();
        }
    }

    IgnoreRegionConfig makeIgnoreConfig(const RoiEditor& editor)
    {
        IgnoreRegionConfig config;
        config.regions = editor.committedRegions;
        config.overlapThreshold = 0.5;
        return config;
    }

    void applyIgnoreConfig(
        const RoiEditor& editor,
        FireDetectionRuntime& fireRuntime,
        SmokeDetectionRuntime& smokeRuntime)
    {
        const IgnoreRegionConfig config = makeIgnoreConfig(editor);
        fireRuntime.setIgnoreRegionConfig(config);
        smokeRuntime.setIgnoreRegionConfig(0, config);
        std::cout << "[ROI] applied regions=" << config.regions.size()
            << " overlapThreshold=" << config.overlapThreshold << '\n';
    }

    void drawRoiEditor(cv::Mat& display, const RoiEditor& editor)
    {
        auto drawPolygon = [&](const std::vector<cv::Point2f>& normalized, bool closed,
            const cv::Scalar& color)
        {
            std::vector<cv::Point> points;
            points.reserve(normalized.size());
            for (const cv::Point2f& point : normalized)
                points.push_back(normalizedPoint(point.x, point.y, display.size()));

            if (points.size() >= 2)
                cv::polylines(display, points, closed, color, 2, cv::LINE_AA);
            for (const cv::Point& point : points)
                cv::circle(display, point, 4, color, cv::FILLED, cv::LINE_AA);
        };

        for (const IgnoreRegion& region : editor.committedRegions)
            drawPolygon(region.points, true, cv::Scalar(255, 0, 255));
        drawPolygon(editor.draftPoints, false, cv::Scalar(0, 255, 255));
    }

    void drawFireBox(
        cv::Mat& image,
        const DetectionBox& box,
        bool showAnalysis)
    {
        const cv::Rect clipped = box.box & cv::Rect(0, 0, image.cols, image.rows);
        if (clipped.empty())
            return;

        cv::rectangle(image, clipped, cv::Scalar(0, 0, 255), 2);
        std::ostringstream label;
        label << "FIRE G" << box.trackId
            << " x" << box.groupedTrackCount << " "
            << std::fixed << std::setprecision(2)
            << box.score << " " << fireConfirmReasonName(box.confirmReason);
        if (box.gridPositionValid)
            label << " GRID(" << box.gridX << ',' << box.gridY << ')';
        else
            label << " GRID(-)";
        if (box.factoryFootprintValid)
            label << " AREA " << std::setprecision(2)
                << box.estimatedGroundAreaSquareMetres << "m2"
                << " R" << box.displayRadiusCells;
        drawOutlinedText(image, label.str(),
            cv::Point(clipped.x, std::max(22, clipped.y - 8)), 0.55, cv::Scalar(0, 0, 255));

        if (!showAnalysis)
            return;

        if (box.fireFootprintValid)
        {
            const cv::Point footprintLeft = normalizedPoint(
                box.footprintLeftNormalizedX, box.footprintNormalizedY, image.size());
            const cv::Point footprintRight = normalizedPoint(
                box.footprintRightNormalizedX, box.footprintNormalizedY, image.size());
            cv::line(image, footprintLeft, footprintRight,
                cv::Scalar(0, 165, 255), 4, cv::LINE_AA);
            const cv::Point footprintMiddle(
                (footprintLeft.x + footprintRight.x) / 2,
                (footprintLeft.y + footprintRight.y) / 2);
            drawOutlinedText(image, "FOOTPRINT", footprintMiddle + cv::Point(8, 18),
                0.48, cv::Scalar(0, 165, 255));
        }

        if (box.representativePositionValid)
        {
            const cv::Point current = normalizedPoint(
                box.representativeNormalizedX, box.representativeNormalizedY, image.size());
            cv::circle(image, current, 5, cv::Scalar(0, 255, 0), cv::FILLED, cv::LINE_AA);
        }


        if (box.smoothedRepresentativePositionValid)
        {
            const cv::Point stable = normalizedPoint(
                box.smoothedRepresentativeNormalizedX,
                box.smoothedRepresentativeNormalizedY,
                image.size());
            cv::drawMarker(image, stable, cv::Scalar(0, 255, 255),
                cv::MARKER_CROSS, 14, 2, cv::LINE_AA);
            drawOutlinedText(image, "STABLE", stable + cv::Point(8, -10),
                0.48, cv::Scalar(0, 255, 255));
        }

    }

    void drawSmokeBox(cv::Mat& image, const DetectionBox& box)
    {
        const cv::Rect clipped = box.box & cv::Rect(0, 0, image.cols, image.rows);
        if (clipped.empty())
            return;
        cv::rectangle(image, clipped, cv::Scalar(255, 255, 0), 2);
        std::ostringstream label;
        label << "SMOKE id=" << box.trackId << " "
            << std::fixed << std::setprecision(2) << box.score;
        drawOutlinedText(image, label.str(),
            cv::Point(clipped.x, std::max(22, clipped.y - 8)), 0.55, cv::Scalar(255, 255, 0));
    }

    void printFireSnapshot(const FireRuntimeSnapshot& snapshot)
    {
        std::cout << "\n[FIRE frame=" << snapshot.resultFrameId
            << "] alarm=" << snapshot.alarm.alarmActive
            << " candidate=" << snapshot.detection.candidate
            << " boxes=" << snapshot.detection.boxes.size() << '\n';
        for (const DetectionBox& box : snapshot.detection.boxes)
        {
            std::cout << "  group=" << box.trackId
                << " tracks=" << box.groupedTrackCount
                << std::fixed << std::setprecision(3)
                << " box=(" << box.box.x << ',' << box.box.y << ','
                << box.box.width << ',' << box.box.height << ')'
                << " grid=";
            if (box.gridPositionValid)
                std::cout << '(' << box.gridX << ',' << box.gridY << ')';
            else
                std::cout << "invalid";
            if (box.factoryPositionValid)
                std::cout << " factory=(" << box.factoryXMetres << "m,"
                    << box.factoryYMetres << "m)";
            if (box.factoryFootprintValid)
                std::cout << " groundWidth=" << box.estimatedGroundWidthMetres
                    << "m groundArea=" << box.estimatedGroundAreaSquareMetres
                    << "m2 displayRadius=" << box.displayRadiusCells << "cells";
            std::cout
                << " score=" << box.score
                << " confirmReason=" << fireConfirmReasonName(box.confirmReason)
                << " reflection=" << box.reflectionLikeCandidate
                << " tinyFlame=" << box.tinyFlameEvidence
                << " dynamicColor=" << box.dynamicColorFlameEvidence
                << " dynamicHistory=" << box.dynamicColorHistoryHits
                << '/' << box.dynamicColorHistorySize
                << " anchorStable=" << box.stableFlameAnchor
                << " structureHits=" << box.flameStructureHits
                << " reflectionHits=" << box.reflectionHits
                << " color=" << box.colorRatio
                << " red=" << box.redOrangeRatio
                << " white=" << box.whiteCoreRatio
                << " tiny=" << box.tinyFlameRatio
                << " skin=" << box.skinRatio
                << " motion=" << box.motionRatio
                << " maskChange=" << box.maskChangeRatio
                << " vStd=" << box.valueStdDev
                << " brightness=" << box.candidateBrightness
                << " bg=" << box.surroundingBrightness
                << " brightnessRatio=" << box.brightnessRatio
                << " pos=(" << box.representativeNormalizedX << ','
                << box.representativeNormalizedY << ')'
                << " stable=(" << box.smoothedRepresentativeNormalizedX << ','
                << box.smoothedRepresentativeNormalizedY << ')'
                << " history=" << box.positionHistory.size()
                << " footprint=(" << box.footprintLeftNormalizedX << ','
                << box.footprintRightNormalizedX << ','
                << box.footprintNormalizedY << ')' << '\n';
        }

        const CameraHealthStatus& camera = snapshot.cameraHealth;
        std::cout << "  camera=" << cameraIssueName(camera.issue)
            << " warning=" << camera.contaminationSuspected
            << " suspectFrames=" << camera.consecutiveSuspectFrames
            << std::fixed << std::setprecision(3)
            << " lapVar=" << camera.laplacianVariance
            << " dark=" << camera.darkPixelRatio
            << " bright=" << camera.brightPixelRatio << '\n';

        const ArucoMappingStatus& aruco = snapshot.arucoMapping;
        std::cout << "  aruco configured=" << aruco.configured
            << " valid=" << aruco.homographyValid
            << " fresh=" << aruco.homographyFresh
            << " static=" << aruco.staticHomography
            << " markers=" << aruco.acceptedMarkers << '/' << aruco.detectedMarkers
            << " inlierCorners=" << aruco.inlierCorners
            << " rms=" << aruco.reprojectionRmsPx << "px"
            << " age=" << aruco.homographyAgeMs << "ms"
            << " lensCalib=" << aruco.lensCalibrationConfigured
            << '/' << aruco.lensCalibrationApplied
            << " lensRms=" << aruco.lensCalibrationRmsPx << "px"
            << " message=\"" << aruco.message << "\"\n";
    }

    void printSmokeSnapshot(const SmokeRuntimeSnapshot& snapshot)
    {
        std::cout << "\n[SMOKE frame=" << snapshot.resultFrameId
            << "] detected=" << snapshot.smokeDetected
            << " boxes=" << snapshot.detection.boxes.size()
            << " score=" << std::fixed << std::setprecision(3) << snapshot.smokeScore
            << " hits=" << snapshot.positiveHits
            << " misses=" << snapshot.consecutiveMisses << '\n';
        for (const DetectionBox& box : snapshot.detection.boxes)
        {
            std::cout << "  track=" << box.trackId
                << " box=(" << box.box.x << ',' << box.box.y << ','
                << box.box.width << ',' << box.box.height << ')'
                << " score=" << box.score << '\n';
        }
    }

    cv::Mat makeDisplayFrame(const cv::Mat& source)
    {
        const double scale = std::min({
            1.0,
            static_cast<double>(MAX_DISPLAY_WIDTH) / source.cols,
            static_cast<double>(MAX_DISPLAY_HEIGHT) / source.rows });
        if (scale >= 1.0)
            return source;

        cv::Mat resized;
        cv::resize(source, resized, cv::Size(), scale, scale, cv::INTER_AREA);
        return resized;
    }

    bool isCameraIndex(const std::string& source)
    {
        return !source.empty() && std::all_of(source.begin(), source.end(),
            [](unsigned char ch) { return std::isdigit(ch) != 0; });
    }

    bool isNetworkCamera(const std::string& source)
    {
        std::string lower = source;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return lower.rfind("rtsp://", 0) == 0 || lower.rfind("rtsps://", 0) == 0;
    }

    std::filesystem::path executableDirectory(const char* executable)
    {
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(executable, error);
        return error ? std::filesystem::current_path() : absolute.parent_path();
    }

    std::string environmentValue(const char* name)
    {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
            return {};
        const std::string result(value);
        std::free(value);
        return result;
#else
        const char* value = std::getenv(name);
        return value == nullptr ? std::string{} : std::string(value);
#endif
    }
}

int main(int argc, char** argv)
{
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);
    std::cout << "Detection Debug Runner\n"
        << "source: video path or camera index (example: 0)\n";

    std::string source;
    if (argc >= 2)
        source = argv[1];
    else
    {
        std::cout << "Input source: ";
        std::getline(std::cin, source);
    }
    if (source.empty())
        source = "0";

    std::uint64_t maxFrames = 0;
    bool headless = false;
    std::string arucoConfigPath;
    std::string cameraCalibrationPath;
    std::string staticHomographyPath;
    std::size_t logicalChannel = 2;
    for (int index = 2; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--max-frames" && index + 1 < argc)
            maxFrames = static_cast<std::uint64_t>(std::stoull(argv[++index]));
        else if (argument == "--headless")
            headless = true;
        else if (argument == "--aruco-config" && index + 1 < argc)
            arucoConfigPath = argv[++index];
        else if (argument == "--camera-calibration" && index + 1 < argc)
            cameraCalibrationPath = argv[++index];
        else if (argument == "--static-homography" && index + 1 < argc)
            staticHomographyPath = argv[++index];
        else if (argument == "--channel" && index + 1 < argc)
        {
            const int parsedChannel = std::stoi(argv[++index]);
            if (parsedChannel < 1 || parsedChannel > 4)
            {
                std::cerr << "--channel must be between 1 and 4.\n";
                return 2;
            }
            logicalChannel = static_cast<std::size_t>(parsedChannel);
        }
    }

    const bool syntheticReflection = source == "synthetic-reflection";
    const bool syntheticMovingFire = source == "synthetic-moving-fire";
    const bool syntheticSpreadingFire = source == "synthetic-spreading-fire";
    const bool syntheticSource = syntheticReflection || syntheticMovingFire ||
        syntheticSpreadingFire;
    const bool localCamera = isCameraIndex(source);
    const bool liveSource = localCamera || isNetworkCamera(source);
    cv::VideoCapture capture;
    if (syntheticReflection)
    {
        std::cout << "Using synthetic skin/orange reflection regression source\n";
    }
    else if (syntheticMovingFire)
    {
        std::cout << "Using synthetic moving-fire direction source\n";
    }
    else if (syntheticSpreadingFire)
    {
        std::cout << "Using synthetic right-spreading fire source\n";
    }
    else if (localCamera)
    {
        const int cameraIndex = std::stoi(source);
#if defined(_WIN32)
        // Avoid OpenCV's automatic backend probing on Windows. Some vendor camera
        // filters can crash inside their DLL before VideoCapture returns an error.
        std::cout << "Opening camera " << cameraIndex << " with DirectShow...\n";
        capture.open(cameraIndex, cv::CAP_DSHOW);
#else
        capture.open(cameraIndex);
#endif
    }
    else if (isNetworkCamera(source))
    {
        // FFmpeg에서 RTSP 열기/읽기가 무기한 대기하지 않도록 제한한다.
        // 읽기 제한시간이 지나면 LatestFrameReader가 종료되어 UI도 정상 종료된다.
        const std::vector<int> captureParameters = {
            cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
            cv::CAP_PROP_READ_TIMEOUT_MSEC, 2000,
        };
        capture.open(source, cv::CAP_FFMPEG, captureParameters);
    }
    else
        capture.open(source);
    if (!syntheticSource && !capture.isOpened())
    {
        std::cerr << "Failed to open source: " << source << '\n';
#if defined(_WIN32)
        if (isCameraIndex(source))
            std::cerr << "DirectShow could not open this camera. Check the camera index, "
                "Windows camera permission, and whether another program is using it.\n";
#endif
        return 1;
    }
    if (liveSource)
        capture.set(cv::CAP_PROP_BUFFERSIZE, 1);

    const std::filesystem::path modelDirectory =
        executableDirectory(argv[0]) / "models" /
        "smoke_yolov8n_public_640x384_ncnn_model";
    const std::string paramPath =
        (modelDirectory / "model.ncnn.param").string();
    const std::string binPath =
        (modelDirectory / "model.ncnn.bin").string();

    FireDetectionRuntime fireRuntime;
    if (arucoConfigPath.empty())
        arucoConfigPath = environmentValue("FIRE_ARUCO_CONFIG_PATH");
    if (arucoConfigPath.empty())
    {
        const std::filesystem::path adjacentConfig =
            executableDirectory(argv[0]) / "aruco_board_config.txt";
        if (std::filesystem::exists(adjacentConfig))
            arucoConfigPath = adjacentConfig.string();
    }
    if (!arucoConfigPath.empty())
    {
        if (fireRuntime.loadArucoBoardConfiguration(
            arucoConfigPath, logicalChannel - 1))
        {
            std::cout << "[ARUCO] configured logical channel " << logicalChannel
                << " from " << arucoConfigPath << '\n';
        }
        else
        {
            std::cerr << "[ARUCO] configuration failed: "
                << fireRuntime.arucoMappingError() << '\n';
        }
    }
    else
    {
        std::cout << "[ARUCO] coordinate output disabled. Use "
            << "--aruco-config <file> --channel <1..4>.\n";
    }
    if (!cameraCalibrationPath.empty())
    {
        if (fireRuntime.loadCameraCalibration(
            cameraCalibrationPath, logicalChannel - 1))
        {
            std::cout << "[CALIBRATION] loaded for logical channel "
                << logicalChannel << " from " << cameraCalibrationPath << '\n';
        }
        else
        {
            std::cerr << "[CALIBRATION] rejected: "
                << fireRuntime.cameraCalibrationError() << '\n';
        }
    }
    if (!staticHomographyPath.empty())
    {
        if (fireRuntime.loadStaticHomography(
            staticHomographyPath, logicalChannel - 1))
        {
            std::cout << "[HOMOGRAPHY] static mapping loaded for logical channel "
                << logicalChannel << " from " << staticHomographyPath << '\n';
        }
        else
        {
            std::cerr << "[HOMOGRAPHY] static mapping rejected: "
                << fireRuntime.arucoMappingError() << '\n';
        }
    }
    SmokeDetectionRuntime smokeRuntime(1, paramPath, binPath);
    if (!smokeRuntime.isModelReady())
        std::cerr << "[SMOKE] model error: " << smokeRuntime.modelError() << '\n';

    RoiEditor roiEditor;
    if (!headless)
    {
        cv::namedWindow(WINDOW_NAME, cv::WINDOW_AUTOSIZE);
        cv::setMouseCallback(WINDOW_NAME, onMouse, &roiEditor);
    }

    cv::Mat frame;
    std::uint64_t frameId = 0;
    std::uint64_t lastPrintedFireFrameId = 0;
    std::uint64_t lastPrintedSmokeFrameId = 0;
    bool paused = false;
    bool showFireAnalysis = false;
    std::uint64_t liveSequence = 0;
    const double sourceFps = syntheticSource ? 30.0 : capture.get(cv::CAP_PROP_FPS);
    std::unique_ptr<LatestFrameReader> liveReader;
    if (liveSource)
        liveReader = std::make_unique<LatestFrameReader>(capture);

    const int waitMs = liveSource ? 1 :
        (sourceFps >= 1.0 && sourceFps <= 120.0 ?
            std::clamp(static_cast<int>(std::lround(1000.0 / sourceFps)), 1, 100) : 30);

    std::cout << "Keys: Q/Esc quit, Space pause, D fire analysis, "
        "R apply ROI, U undo ROI, C clear ROI\n";
    while (true)
    {
        bool newFrame = false;
        if (!paused)
        {
            if (liveReader)
            {
                if (liveReader->getLatest(frame, liveSequence))
                {
                    frameId = liveSequence;
                    newFrame = true;
                }
                else
                {
                    if (liveReader->ended())
                        break;
                    const int waitingKey = headless ? -1 : (cv::waitKey(1) & 0xff);
                    if (waitingKey == 27 || waitingKey == 'q' || waitingKey == 'Q')
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
            }
            else if (syntheticReflection)
            {
                ++frameId;
                frame = cv::Mat(360, 640, CV_8UC3, cv::Scalar(55, 70, 80));
                cv::ellipse(frame, cv::Point(320, 210), cv::Size(150, 95),
                    0.0, 0.0, 360.0, cv::Scalar(95, 145, 195), cv::FILLED);
                const int x = 300 + static_cast<int>(std::lround(5.0 * std::sin(frameId * 0.18)));
                const int y = 180 + static_cast<int>(std::lround(3.0 * std::sin(frameId * 0.12)));
                const int radius = 10 + static_cast<int>(std::lround(2.0 * std::sin(frameId * 0.25)));
                cv::circle(frame, cv::Point(x, y), radius,
                    cv::Scalar(35, 145, 245), cv::FILLED);
                cv::circle(frame, cv::Point(x - 2, y - 2), std::max(2, radius / 3),
                    cv::Scalar(235, 240, 250), cv::FILLED);
                newFrame = true;
            }
            else if (syntheticMovingFire)
            {
                ++frameId;
                frame = cv::Mat(360, 640, CV_8UC3, cv::Scalar(35, 40, 45));
                const int x = 100 + static_cast<int>(frameId / 2);
                const int baseY = 270;
                const int flicker = static_cast<int>(std::lround(
                    4.0 * std::sin(frameId * 0.37)));
                std::vector<cv::Point> flame = {
                    cv::Point(x - 16, baseY),
                    cv::Point(x - 12, baseY - 28),
                    cv::Point(x - 4, baseY - 48 - flicker),
                    cv::Point(x + 2, baseY - 30),
                    cv::Point(x + 10, baseY - 58 + flicker),
                    cv::Point(x + 17, baseY - 22),
                    cv::Point(x + 18, baseY)
                };
                cv::fillConvexPoly(frame, flame, cv::Scalar(20, 110, 245), cv::LINE_AA);
                cv::ellipse(frame, cv::Point(x + 1, baseY - 13), cv::Size(7, 17),
                    0.0, 0.0, 360.0, cv::Scalar(40, 205, 255), cv::FILLED, cv::LINE_AA);
                cv::ellipse(frame, cv::Point(x + 1, baseY - 8), cv::Size(3, 8),
                    0.0, 0.0, 360.0, cv::Scalar(240, 245, 255), cv::FILLED, cv::LINE_AA);
                newFrame = true;
            }
            else if (syntheticSpreadingFire)
            {
                ++frameId;
                frame = cv::Mat(360, 640, CV_8UC3, cv::Scalar(35, 40, 45));
                const int left = 170;
                const int right = 215 + std::min<int>(150, static_cast<int>(frameId / 2));
                const int baseY = 278;
                const int flicker = static_cast<int>(std::lround(
                    3.0 * std::sin(frameId * 0.31)));
                const cv::Scalar outerColor = frameId % 2 == 0 ?
                    cv::Scalar(20, 110, 245) : cv::Scalar(25, 130, 255);
                std::vector<cv::Point> flame = {
                    cv::Point(left, baseY),
                    cv::Point(left + 8, baseY - 48 - flicker),
                    cv::Point((left + right) / 2, baseY - 62 + flicker),
                    cv::Point(right - 8, baseY - 44 - flicker),
                    cv::Point(right, baseY)
                };
                cv::fillConvexPoly(frame, flame, outerColor, cv::LINE_AA);
                const int coreX = left + (right - left) / 2;
                cv::ellipse(frame, cv::Point(coreX, baseY - 13),
                    cv::Size(std::max(5, (right - left) / 8), 17),
                    0.0, 0.0, 360.0, cv::Scalar(40, 205, 255), cv::FILLED, cv::LINE_AA);
                newFrame = true;
            }
            else
            {
                if (!capture.read(frame) || frame.empty())
                    break;
                ++frameId;
                newFrame = true;
            }
        }
        if (frame.empty())
            continue;

        const auto now = FireDetectionRuntime::Clock::now();
        if (newFrame)
        {
            fireRuntime.submitFrame(frame, frameId, now);
            smokeRuntime.submitFrame(0, frame, frameId, now);
        }

        const FireRuntimeSnapshot fire = fireRuntime.poll(now);
        const SmokeRuntimeSnapshot smoke = smokeRuntime.poll(0, now);
        if (fire.hasResult && fire.resultFrameId != 0 &&
            fire.resultFrameId != lastPrintedFireFrameId)
        {
            printFireSnapshot(fire);
            lastPrintedFireFrameId = fire.resultFrameId;
        }
        if (smoke.hasResult && smoke.resultFrameId != 0 &&
            smoke.resultFrameId != lastPrintedSmokeFrameId)
        {
            printSmokeSnapshot(smoke);
            lastPrintedSmokeFrameId = smoke.resultFrameId;
        }

        if (!headless)
        {
            cv::Mat annotated = frame.clone();
            if (fire.resultIsFresh)
                for (const DetectionBox& box : fire.detection.boxes)
                    drawFireBox(annotated, box, showFireAnalysis);
            if (smoke.resultIsFresh)
                for (const DetectionBox& box : smoke.detection.boxes)
                    drawSmokeBox(annotated, box);

            std::ostringstream status;
            status << "FIRE " << (fire.alarm.alarmActive ? "DETECTED" : "-")
                << " | SMOKE " << (smoke.smokeDetected ? "DETECTED" : "-")
                << " | ROI " << roiEditor.committedRegions.size()
                << " | ARUCO ";
            if (!fire.arucoMapping.configured)
                status << "OFF";
            else if (fire.arucoMapping.homographyFresh)
                status << "OK " << fire.arucoMapping.acceptedMarkers << "M";
            else
                status << "WAIT " << fire.arucoMapping.acceptedMarkers
                    << '/' << fire.arucoMapping.detectedMarkers << "M";
            drawOutlinedText(annotated, status.str(), cv::Point(16, 30),
                0.65, fire.alarm.alarmActive ? cv::Scalar(0, 0, 255) : cv::Scalar(255, 255, 255));

            const CameraHealthStatus& camera = fire.cameraHealth;
            std::ostringstream cameraText;
            if (camera.contaminationSuspected)
                cameraText << "CAMERA WARNING: " << cameraIssueName(camera.issue);
            else if (camera.consecutiveSuspectFrames > 0)
                cameraText << "CAMERA CHECKING " << camera.consecutiveSuspectFrames;
            else
                cameraText << "CAMERA OK";
            drawOutlinedText(annotated, cameraText.str(), cv::Point(16, 58), 0.60,
                camera.contaminationSuspected ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0));
            drawOutlinedText(annotated,
                "L-click ROI | R apply | U undo | C clear | D analysis | Space pause | Q quit",
                cv::Point(16, annotated.rows - 18), 0.48, cv::Scalar(255, 255, 255));

            cv::Mat display = makeDisplayFrame(annotated);
            roiEditor.displaySize = display.size();
            drawRoiEditor(display, roiEditor);
            cv::imshow(WINDOW_NAME, display);
        }

        if (maxFrames > 0 && frameId >= maxFrames)
            break;

        if (headless && !liveSource)
            std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
        const int key = headless ? -1 : (cv::waitKey(paused ? 30 : waitMs) & 0xff);
        if (key == 27 || key == 'q' || key == 'Q')
            break;
        if (key == ' ')
            paused = !paused;
        else if (key == 'd' || key == 'D')
        {
            showFireAnalysis = !showFireAnalysis;
            std::cout << "[VIEW] fire analysis overlay="
                << (showFireAnalysis ? "ON" : "OFF") << '\n';
        }
        else if (key == 'r' || key == 'R')
        {
            if (roiEditor.draftPoints.size() >= 3)
            {
                IgnoreRegion region;
                region.enabled = true;
                region.points = roiEditor.draftPoints;
                roiEditor.committedRegions.push_back(std::move(region));
                roiEditor.draftPoints.clear();
                applyIgnoreConfig(roiEditor, fireRuntime, smokeRuntime);
            }
            else
            {
                std::cout << "[ROI] at least 3 points are required\n";
            }
        }
        else if (key == 'u' || key == 'U')
        {
            if (!roiEditor.draftPoints.empty())
                roiEditor.draftPoints.pop_back();
            else if (!roiEditor.committedRegions.empty())
            {
                roiEditor.committedRegions.pop_back();
                applyIgnoreConfig(roiEditor, fireRuntime, smokeRuntime);
            }
        }
        else if (key == 'c' || key == 'C')
        {
            roiEditor.draftPoints.clear();
            roiEditor.committedRegions.clear();
            applyIgnoreConfig(roiEditor, fireRuntime, smokeRuntime);
        }
    }

    if (liveReader)
        liveReader->stop();
    smokeRuntime.stop();
    fireRuntime.stop();
    if (!headless)
        cv::destroyAllWindows();
    return 0;
}
