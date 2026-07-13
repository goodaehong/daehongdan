#include <opencv2/opencv.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

#include "AppConfig.h"
#include "CameraStream.h"
#include "DisplayUtils.h"
#include "FireDetector_1.h"

using namespace cv;
using namespace std;

#if FIRE_DEBUG_VIEW
namespace
{
    Mat makeDebugTile(
        const Mat& source,
        const string& title
    )
    {
        const Size tileSize(480, 270);

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

            resize(
                gray,
                gray,
                tileSize,
                0,
                0,
                INTER_NEAREST
            );
        }

        Mat tile;
        cvtColor(gray, tile, COLOR_GRAY2BGR);

        putText(
            tile,
            title,
            Point(12, 28),
            FONT_HERSHEY_SIMPLEX,
            0.70,
            Scalar(0, 255, 255),
            2
        );

        return tile;
    }

    Mat makeDebugPanel(
        const FireDebugImages& debug
    )
    {
        Mat fireTile =
            makeDebugTile(
                debug.fireColorMask,
                "Fire color mask"
            );

        Mat skinTile =
            makeDebugTile(
                debug.skinMask,
                "Skin mask"
            );

        Mat foregroundTile =
            makeDebugTile(
                debug.foregroundMask,
                "Foreground mask"
            );

        Mat candidateTile =
            makeDebugTile(
                debug.candidateMask,
                "Candidate mask"
            );

        Mat topRow;
        Mat bottomRow;
        Mat panel;

        hconcat(fireTile, skinTile, topRow);
        hconcat(foregroundTile, candidateTile, bottomRow);
        vconcat(topRow, bottomRow, panel);

        return panel;
    }
}
#endif

int main()
{
#ifdef _WIN32
    _putenv_s(
        "OPENCV_FFMPEG_CAPTURE_OPTIONS",
        "rtsp_transport;udp|fflags;nobuffer|flags;low_delay|max_delay;0|analyzeduration;0|probesize;2048"
    );
#else
    setenv(
        "OPENCV_FFMPEG_CAPTURE_OPTIONS",
        "rtsp_transport;udp|fflags;nobuffer|flags;low_delay|max_delay;0|analyzeduration;0|probesize;2048",
        1
    );
#endif

    string cameraIp;

    cout << "Camera IP 입력: ";
    cin >> cameraIp;

    const string url =
        "rtsp://admin:5hanwha!@" +
        cameraIp +
        ":554/0/profile2/media.smp";

    CameraStream camera(url);

    if (!camera.start())
    {
        cerr << "Camera thread start failed" << endl;
        return -1;
    }

    cout << "Waiting for camera frame..." << endl;

    Mat firstFrame;
    uint64_t lastFrameId = 0;
    const auto waitStart = chrono::steady_clock::now();

    while (!camera.getLatestFrame(firstFrame, lastFrameId))
    {
        const double waitSeconds =
            chrono::duration<double>(
                chrono::steady_clock::now() - waitStart
            ).count();

        if (waitSeconds >= 10.0)
            break;

        this_thread::sleep_for(chrono::milliseconds(10));
    }

    if (firstFrame.empty())
    {
        cerr << "Camera frame receive failed" << endl;
        camera.stop();
        return -1;
    }

    cout << "Camera frame receive success" << endl;
    cout << "Fire Detector Build: V14_FINAL_ALARM_GATE" << endl;

    // ==================================================
    // 화염 검출 작업 스레드용 공유 상태
    // ==================================================
    FireDetector fireDetector;

    mutex jobMutex;
    condition_variable jobCondition;
    Mat pendingFrame;
    uint64_t pendingFrameId = 0;
    bool jobReady = false;
    atomic<bool> detectorRunning{ true };

    mutex resultMutex;
    DetectionResult latestFireResult;
    uint64_t latestResultFrameId = 0;
    double latestDetectMs = 0.0;
    chrono::steady_clock::time_point latestResultTime;
    bool hasDetectionResult = false;

    thread detectorThread(
        [&]()
        {
            while (true)
            {
                Mat detectFrame;
                uint64_t detectFrameId = 0;

                {
                    unique_lock<mutex> lock(jobMutex);

                    jobCondition.wait(
                        lock,
                        [&]()
                        {
                            return !detectorRunning.load() || jobReady;
                        }
                    );

                    if (!detectorRunning.load() && !jobReady)
                        break;

                    // 큐를 쌓지 않고 가장 최신 프레임 하나만 가져간다.
                    detectFrame = pendingFrame;
                    detectFrameId = pendingFrameId;
                    jobReady = false;
                }

                if (detectFrame.empty())
                    continue;

                const auto detectStart =
                    chrono::steady_clock::now();

                DetectionResult result =
                    fireDetector.detect(detectFrame);

                const double detectMs =
                    chrono::duration<double, milli>(
                        chrono::steady_clock::now() - detectStart
                    ).count();

                {
                    lock_guard<mutex> lock(resultMutex);
                    latestFireResult = std::move(result);
                    latestResultFrameId = detectFrameId;
                    latestDetectMs = detectMs;
                    latestResultTime = chrono::steady_clock::now();
                    hasDetectionResult = true;
                }
            }
        }
    );

    auto previousDisplayTime =
        chrono::steady_clock::now();

    double averageDisplayFps = 0.0;
    double averageDetectMs = 0.0;

    // ==================================================
    // 최종 화재 경보 게이트
    // FireDetector의 순간 오탐을 바로 최종 경보로 사용하지 않고,
    // 서로 다른 검출 결과에서 일정 시간 이상 지속될 때만 확정한다.
    // ==================================================
    constexpr double FINAL_CONFIRM_MS = 600.0;
    constexpr double FINAL_RELEASE_MS = 250.0;
    constexpr int MIN_RAW_FIRE_RESULTS = 2;

    bool finalFireAlarm = false;
    bool rawFireTiming = false;
    bool rawFireLostTiming = false;

    int rawFireResultCount = 0;
    uint64_t lastAlarmProcessedResultFrameId = 0;

    auto rawFireStartTime = chrono::steady_clock::now();
    auto rawFireLostStartTime = chrono::steady_clock::now();

    while (true)
    {
        Mat frame;

        if (!camera.getLatestFrame(frame, lastFrameId))
        {
            const char key =
                static_cast<char>(waitKey(1));

            if (key == 'q' || key == 27)
                break;

            this_thread::sleep_for(
                chrono::milliseconds(1)
            );

            continue;
        }

        if (frame.empty())
            continue;

        const auto now =
            chrono::steady_clock::now();

        // ==================================================
        // 새 카메라 프레임을 항상 검출 스레드에 전달한다.
        // 검출기가 처리 중이면 큐를 쌓지 않고 pendingFrame만
        // 가장 최신 프레임으로 덮어쓴다.
        // 따라서 화면은 부드럽게 유지하면서 검출기는 가능한
        // 최대 속도로 연속 프레임을 처리한다.
        // ==================================================
        {
            lock_guard<mutex> lock(jobMutex);
            pendingFrame = frame;
            pendingFrameId = lastFrameId;
            jobReady = true;
        }

        jobCondition.notify_one();

        // 검출 스레드가 마지막으로 완료한 결과를 가져온다.
        DetectionResult displayResult;
        uint64_t resultFrameId = 0;
        double detectMs = 0.0;
        chrono::steady_clock::time_point resultTime;
        bool hasResult = false;

        {
            lock_guard<mutex> lock(resultMutex);
            displayResult = latestFireResult;
            resultFrameId = latestResultFrameId;
            detectMs = latestDetectMs;
            resultTime = latestResultTime;
            hasResult = hasDetectionResult;
        }

        averageDetectMs =
            averageDetectMs <= 0.0
            ? detectMs
            : averageDetectMs * 0.90 + detectMs * 0.10;

        const double displayIntervalSec =
            chrono::duration<double>(
                now - previousDisplayTime
            ).count();

        previousDisplayTime = now;

        const double displayFps =
            displayIntervalSec > 0.0
            ? 1.0 / displayIntervalSec
            : 0.0;

        averageDisplayFps =
            averageDisplayFps <= 0.0
            ? displayFps
            : averageDisplayFps * 0.90 + displayFps * 0.10;

        Mat displaySource = frame.clone();

        // 검출 결과의 유효성을 프레임 개수가 아니라 실제 시간으로 판단한다.
        // 검출 처리시간이 길면 프레임 번호 차이는 쉽게 10을 넘기 때문에
        // 기존 resultAge <= 10 조건은 정상 검출 결과까지 버릴 수 있었다.
        const uint64_t resultAgeFrames =
            lastFrameId >= resultFrameId
            ? lastFrameId - resultFrameId
            : 0;

        const double resultAgeMs =
            hasResult
            ? chrono::duration<double, milli>(now - resultTime).count()
            : 0.0;

        // 경보 문구는 최대 1초간 유지하되, 박스는 훨씬 짧게 유지한다.
        // 검출 시간이 긴 환경에서도 박스가 깜빡이지 않도록
        // 최근 평균 검출시간의 1.4배를 사용하되 180~450ms로 제한한다.
        const bool resultIsFresh =
            hasResult && resultAgeMs <= 1000.0;

        const double boxFreshLimitMs =
            std::clamp(
                averageDetectMs * 1.4,
                180.0,
                450.0
            );

        // ==================================================
        // 새 검출 결과가 도착했을 때만 경보 누적 상태를 갱신한다.
        // 같은 결과를 화면 FPS만큼 반복 계산하지 않기 위함이다.
        // ==================================================
        const bool hasNewDetectionResult =
            hasResult &&
            resultFrameId != 0 &&
            resultFrameId != lastAlarmProcessedResultFrameId;

        if (hasNewDetectionResult)
        {
            lastAlarmProcessedResultFrameId = resultFrameId;

            const bool rawFireDetected =
                resultIsFresh &&
                displayResult.detected &&
                !displayResult.boxes.empty();

            if (rawFireDetected)
            {
                if (!rawFireTiming)
                {
                    rawFireTiming = true;
                    rawFireStartTime = now;
                    rawFireResultCount = 1;
                }
                else
                {
                    ++rawFireResultCount;
                }

                // 음성 결과가 잠깐 들어왔다가 다시 화염으로 돌아오면
                // 해제 타이머를 취소한다.
                rawFireLostTiming = false;
            }
            else
            {
                if (!finalFireAlarm)
                {
                    // 확정 전 후보가 끊기면 처음부터 다시 확인한다.
                    rawFireTiming = false;
                    rawFireResultCount = 0;
                }
                else if (!rawFireLostTiming)
                {
                    // 이미 확정된 경보는 짧은 순간 누락을 허용한다.
                    rawFireLostTiming = true;
                    rawFireLostStartTime = now;
                }
            }
        }

        double pendingFireMs = 0.0;

        if (rawFireTiming)
        {
            pendingFireMs =
                chrono::duration<double, milli>(
                    now - rawFireStartTime
                ).count();
        }

        if (!finalFireAlarm &&
            rawFireTiming &&
            rawFireResultCount >= MIN_RAW_FIRE_RESULTS &&
            pendingFireMs >= FINAL_CONFIRM_MS)
        {
            finalFireAlarm = true;
            rawFireLostTiming = false;
        }

        if (finalFireAlarm && rawFireLostTiming)
        {
            const double lostFireMs =
                chrono::duration<double, milli>(
                    now - rawFireLostStartTime
                ).count();

            if (lostFireMs >= FINAL_RELEASE_MS)
            {
                finalFireAlarm = false;
                rawFireTiming = false;
                rawFireLostTiming = false;
                rawFireResultCount = 0;
            }
        }

        // 검출 결과가 장시간 갱신되지 않으면 경보 상태도 해제한다.
        if (!resultIsFresh)
        {
            finalFireAlarm = false;
            rawFireTiming = false;
            rawFireLostTiming = false;
            rawFireResultCount = 0;
        }

        const bool boxIsFresh =
            finalFireAlarm &&
            resultIsFresh &&
            displayResult.detected &&
            resultAgeMs <= boxFreshLimitMs &&
            !displayResult.boxes.empty();

        if (boxIsFresh)
        {
            drawDetectionResult(
                displaySource,
                displayResult
            );
        }

        Mat display;

        resize(
            displaySource,
            display,
            Size(960, 540),
            0,
            0,
            INTER_AREA
        );

        if (finalFireAlarm)
        {
            putText(
                display,
                "FIRE DETECTED",
                Point(20, 40),
                FONT_HERSHEY_SIMPLEX,
                1.0,
                Scalar(0, 0, 255),
                3
            );
        }
        else if (
            resultIsFresh &&
            resultAgeMs <= boxFreshLimitMs &&
            (
                displayResult.candidateDisplayReady ||
                (
                    rawFireTiming &&
                    pendingFireMs >= FINAL_CONFIRM_MS * 0.65
                    )
                )
            )
        {
            putText(
                display,
                "FIRE CANDIDATE",
                Point(20, 40),
                FONT_HERSHEY_SIMPLEX,
                1.0,
                Scalar(0, 165, 255),
                2
            );
        }
        else
        {
            putText(
                display,
                "NORMAL",
                Point(20, 40),
                FONT_HERSHEY_SIMPLEX,
                1.0,
                Scalar(0, 255, 0),
                2
            );
        }

        char performanceText[160];

        snprintf(
            performanceText,
            sizeof(performanceText),
            "Display %.1f FPS | Detect %.1f ms | Result %.0f ms | Confirm %d | Alarm %.0f/%.0f ms | Raw %d",
            averageDisplayFps,
            averageDetectMs,
            resultAgeMs,
            displayResult.confirmCount,
            pendingFireMs,
            FINAL_CONFIRM_MS,
            rawFireResultCount
        );

        putText(
            display,
            performanceText,
            Point(20, 80),
            FONT_HERSHEY_SIMPLEX,
            0.60,
            Scalar(255, 255, 255),
            2
        );

        imshow("Fire Detection", display);

#if FIRE_DEBUG_VIEW
        // 모든 디버그 창 출력과 waitKey는 메인 스레드에서 처리한다.
        if (hasResult)
        {
            const FireDebugImages& debug =
                displayResult.debugImages;

            const bool hasAnyDebugImage =
                !debug.fireColorMask.empty() ||
                !debug.skinMask.empty() ||
                !debug.foregroundMask.empty() ||
                !debug.candidateMask.empty();

            if (hasAnyDebugImage)
            {
                Mat debugPanel =
                    makeDebugPanel(debug);

                imshow(
                    "Fire Debug Masks",
                    debugPanel
                );
            }
        }
#endif

        const char key =
            static_cast<char>(waitKey(1));

        if (key == 'q' || key == 27)
            break;
    }

    // 검출 스레드 종료
    {
        lock_guard<mutex> lock(jobMutex);
        detectorRunning = false;
        jobReady = false;
    }

    jobCondition.notify_one();

    if (detectorThread.joinable())
        detectorThread.join();

    camera.stop();
    destroyAllWindows();

    return 0;
}