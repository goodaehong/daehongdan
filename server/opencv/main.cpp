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
#if RTSP_USE_UDP
    const char* ffmpegCaptureOptions =
        "rtsp_transport;udp|fflags;nobuffer|flags;low_delay|max_delay;0|analyzeduration;0|probesize;2048";
#else
    const char* ffmpegCaptureOptions =
        "rtsp_transport;tcp|fflags;nobuffer|flags;low_delay|max_delay;100000|analyzeduration;0|probesize;4096";
#endif

#ifdef _WIN32
    _putenv_s(
        "OPENCV_FFMPEG_CAPTURE_OPTIONS",
        ffmpegCaptureOptions
    );
#else
    setenv(
        "OPENCV_FFMPEG_CAPTURE_OPTIONS",
        ffmpegCaptureOptions,
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
    cout << "Fire Detector Build: V19_TRACK_TIME_RECONNECT_FIX" << endl;

    // ==================================================
    // 화염 검출 작업 스레드용 공유 상태
    // ==================================================
    FireDetector fireDetector;

    mutex jobMutex;
    condition_variable jobCondition;
    Mat pendingFrame;
    uint64_t pendingFrameId = 0;
    uint64_t pendingStreamEpoch = 0;
    chrono::steady_clock::time_point pendingFrameTime;
    bool jobReady = false;
    atomic<bool> detectorRunning{ true };
    atomic<bool> detectorResetRequested{ false };
    atomic<uint64_t> streamEpoch{ 0 };

    mutex resultMutex;
    DetectionResult latestFireResult;
    uint64_t latestResultFrameId = 0;
    uint64_t latestResultStreamEpoch = 0;
    double latestDetectMs = 0.0;
    chrono::steady_clock::time_point latestResultSourceTime;
    chrono::steady_clock::time_point latestResultCompletedTime;
    bool hasDetectionResult = false;

    thread detectorThread(
        [&]()
        {
            while (true)
            {
                Mat detectFrame;
                uint64_t detectFrameId = 0;
                uint64_t detectStreamEpoch = 0;
                chrono::steady_clock::time_point detectFrameTime;

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
                    detectStreamEpoch = pendingStreamEpoch;
                    detectFrameTime = pendingFrameTime;
                    jobReady = false;
                }

                if (detectFrame.empty())
                    continue;

                if (detectorResetRequested.exchange(false))
                {
                    fireDetector.reset();
                }

                const auto detectStart =
                    chrono::steady_clock::now();

                DetectionResult result =
                    fireDetector.detect(detectFrame);

                const double detectMs =
                    chrono::duration<double, milli>(
                        chrono::steady_clock::now() - detectStart
                    ).count();

                // 연결이 끊기기 전 프레임을 처리한 결과는 새 연결에 넘기지 않는다.
                if (detectStreamEpoch != streamEpoch.load())
                    continue;

                {
                    lock_guard<mutex> lock(resultMutex);
                    latestFireResult = std::move(result);
                    latestResultFrameId = detectFrameId;
                    latestResultStreamEpoch = detectStreamEpoch;
                    latestDetectMs = detectMs;
                    latestResultSourceTime = detectFrameTime;
                    latestResultCompletedTime = chrono::steady_clock::now();
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
    constexpr double FINAL_CONFIRM_MS = 350.0;
    constexpr double FINAL_RELEASE_MS = 350.0;
    constexpr int MIN_RAW_FIRE_RESULTS = 2;

    // 피부색 또는 노랑/주황색 비율이 높고 실제 화염 중심 구조가 약한 후보는
    // 순간 오탐 가능성이 높으므로 더 오래, 더 여러 번 확인한다.
    constexpr double AMBIGUOUS_CONFIRM_MS = 900.0;
    constexpr int MIN_AMBIGUOUS_RAW_FIRE_RESULTS = 3;
    constexpr double AMBIGUOUS_MIN_SCORE = 0.78;

    bool finalFireAlarm = false;
    bool rawFireTiming = false;
    bool rawFireLostTiming = false;

    int rawFireResultCount = 0;
    uint64_t lastAlarmProcessedResultFrameId = 0;

    double activeConfirmMs = FINAL_CONFIRM_MS;
    int activeMinRawFireResults = MIN_RAW_FIRE_RESULTS;
    bool activeAmbiguousWarmObject = false;

    auto rawFireStartTime = chrono::steady_clock::now();
    auto rawFireLostStartTime = chrono::steady_clock::now();

    bool cameraConnectionWasOpen = true;

    const auto resetFinalAlarmState = [&]()
        {
            finalFireAlarm = false;
            rawFireTiming = false;
            rawFireLostTiming = false;
            rawFireResultCount = 0;
            activeConfirmMs = FINAL_CONFIRM_MS;
            activeMinRawFireResults = MIN_RAW_FIRE_RESULTS;
            activeAmbiguousWarmObject = false;
        };

    while (true)
    {
        Mat frame;

        if (!camera.getLatestFrame(frame, lastFrameId))
        {
            if (!camera.isOpened() && cameraConnectionWasOpen)
            {
                cameraConnectionWasOpen = false;
                streamEpoch.fetch_add(1);
                detectorResetRequested = true;
                resetFinalAlarmState();
                lastAlarmProcessedResultFrameId = 0;

                lock_guard<mutex> lock(resultMutex);
                latestFireResult = DetectionResult{};
                latestResultFrameId = 0;
                latestResultStreamEpoch = streamEpoch.load();
                latestDetectMs = 0.0;
                hasDetectionResult = false;
            }

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

        if (!cameraConnectionWasOpen)
        {
            cameraConnectionWasOpen = true;
            detectorResetRequested = true;
        }

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
            pendingStreamEpoch = streamEpoch.load();
            pendingFrameTime = now;
            jobReady = true;
        }

        jobCondition.notify_one();

        // 검출 스레드가 마지막으로 완료한 결과를 가져온다.
        DetectionResult displayResult;
        uint64_t resultFrameId = 0;
        uint64_t resultStreamEpoch = 0;
        double detectMs = 0.0;
        chrono::steady_clock::time_point resultSourceTime;
        chrono::steady_clock::time_point resultCompletedTime;
        bool hasResult = false;

        {
            lock_guard<mutex> lock(resultMutex);
            displayResult = latestFireResult;
            resultFrameId = latestResultFrameId;
            resultStreamEpoch = latestResultStreamEpoch;
            detectMs = latestDetectMs;
            resultSourceTime = latestResultSourceTime;
            resultCompletedTime = latestResultCompletedTime;
            hasResult =
                hasDetectionResult &&
                resultStreamEpoch == streamEpoch.load();
        }

        if (hasResult && detectMs > 0.0)
        {
            averageDetectMs =
                averageDetectMs <= 0.0
                ? detectMs
                : averageDetectMs * 0.90 + detectMs * 0.10;
        }

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

        // 결과 완료 시각이 아니라 검출에 사용된 원본 프레임의 전달 시각을
        // 기준으로 나이를 계산한다. 처리시간이 긴 결과를 최신 영상으로
        // 오인하여 현재 화면에 과거 박스를 그리는 문제를 막는다.
        const double resultAgeMs =
            hasResult
            ? chrono::duration<double, milli>(now - resultSourceTime).count()
            : 0.0;

        const double completedAgeMs =
            hasResult
            ? chrono::duration<double, milli>(now - resultCompletedTime).count()
            : 0.0;

        const double resultFreshLimitMs =
            std::clamp(
                averageDetectMs * 2.2 + 300.0,
                1000.0,
                2500.0
            );

        const bool resultIsFresh =
            hasResult && resultAgeMs <= resultFreshLimitMs;

        // 원본 프레임 기준 나이를 사용하므로 검출 처리시간 자체를 표시 허용
        // 시간에 포함한다. 그렇지 않으면 검출이 450ms를 넘을 때 박스가
        // 완료되는 순간부터 이미 만료되는 문제가 생긴다.
        const double boxFreshLimitMs =
            std::clamp(
                averageDetectMs * 1.6 + 120.0,
                300.0,
                1200.0
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

            const DetectionBox* primaryBox =
                !displayResult.boxes.empty()
                ? &displayResult.boxes.front()
                : nullptr;

            const bool reflectionRisk =
                primaryBox != nullptr &&
                primaryBox->reflectionLikeCandidate;

            const bool fingerRisk =
                primaryBox != nullptr &&
                primaryBox->fingerLikeCandidate;

            const bool ambiguousWarmObject =
                primaryBox != nullptr &&
                (
                    fingerRisk ||
                    (
                        !primaryBox->coreHaloEvidence &&
                        (
                            reflectionRisk ||
                            primaryBox->skinLikeCandidate ||
                            primaryBox->candidateSkinRatio >= 0.35 ||
                            primaryBox->yellowDominantRatio >= 0.40
                            )
                        )
                    );

            // 피부/노랑/주황 계열의 애매한 후보는 점수까지 충분히 높아야
            // 최종 경보 누적에 참여할 수 있다.
            const bool reflectionDynamicRescue =
                primaryBox != nullptr &&
                reflectionRisk &&
                primaryBox->score >= 0.86 &&
                primaryBox->redOrangeRatio >= 0.24 &&
                (
                    primaryBox->brightnessDiffMean >= 2.8 ||
                    primaryBox->maskChangeRatio >= 0.040
                    );

            const bool passesReflectionGate =
                primaryBox != nullptr &&
                (
                    !reflectionRisk ||
                    (
                        primaryBox->strongFireEvidence &&
                        (
                            primaryBox->coreHaloEvidence ||
                            primaryBox->brightBackgroundEvidence ||
                            reflectionDynamicRescue
                            )
                        )
                    );

            // 손가락 후보는 점수가 높더라도 피부 밖에서 분리된 화염색 층이
            // 확인되지 않으면 최종 경보 누적에서 제외한다.
            const bool passesFingerGate =
                primaryBox != nullptr &&
                (
                    !fingerRisk ||
                    (
                        primaryBox->score >= 0.90 &&
                        primaryBox->strongFireEvidence &&
                        primaryBox->skinSeparatedFlameEvidence
                        )
                    );

            const bool passesRiskGate =
                primaryBox != nullptr &&
                passesReflectionGate &&
                passesFingerGate &&
                (
                    !ambiguousWarmObject ||
                    (
                        primaryBox->score >= AMBIGUOUS_MIN_SCORE &&
                        primaryBox->strongFireEvidence
                        )
                    );

            const bool rawFireDetected =
                resultIsFresh &&
                displayResult.detected &&
                primaryBox != nullptr &&
                passesRiskGate;

            if (rawFireDetected)
            {
                const double requiredConfirmMs =
                    ambiguousWarmObject
                    ? AMBIGUOUS_CONFIRM_MS
                    : FINAL_CONFIRM_MS;

                const int requiredRawResults =
                    ambiguousWarmObject
                    ? MIN_AMBIGUOUS_RAW_FIRE_RESULTS
                    : MIN_RAW_FIRE_RESULTS;

                if (!rawFireTiming)
                {
                    rawFireTiming = true;
                    rawFireStartTime = now;
                    rawFireResultCount = 1;
                    activeConfirmMs = requiredConfirmMs;
                    activeMinRawFireResults = requiredRawResults;
                    activeAmbiguousWarmObject = ambiguousWarmObject;
                }
                else
                {
                    ++rawFireResultCount;

                    // 확인 도중 피부/노랑 계열 위험 후보가 한 번이라도 섞이면
                    // 해당 경보 사이클 전체에 더 엄격한 조건을 적용한다.
                    activeConfirmMs =
                        max(activeConfirmMs, requiredConfirmMs);

                    activeMinRawFireResults =
                        max(activeMinRawFireResults, requiredRawResults);

                    activeAmbiguousWarmObject =
                        activeAmbiguousWarmObject || ambiguousWarmObject;
                }

                // 음성 결과가 잠깐 들어왔다가 다시 화염으로 돌아오면
                // 해제 타이머를 취소한다.
                rawFireLostTiming = false;
            }
            else
            {
                if (!finalFireAlarm)
                {
                    // 확정 전 후보가 끊기거나 위험 게이트를 통과하지 못하면
                    // 처음부터 다시 확인한다.
                    rawFireTiming = false;
                    rawFireResultCount = 0;
                    activeConfirmMs = FINAL_CONFIRM_MS;
                    activeMinRawFireResults = MIN_RAW_FIRE_RESULTS;
                    activeAmbiguousWarmObject = false;
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
            rawFireResultCount >= activeMinRawFireResults &&
            pendingFireMs >= activeConfirmMs)
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
                activeConfirmMs = FINAL_CONFIRM_MS;
                activeMinRawFireResults = MIN_RAW_FIRE_RESULTS;
                activeAmbiguousWarmObject = false;
            }
        }

        // 검출 결과가 장시간 갱신되지 않으면 경보 상태도 해제한다.
        if (!resultIsFresh)
        {
            resetFinalAlarmState();
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
                    pendingFireMs >= activeConfirmMs *
                    (activeAmbiguousWarmObject ? 0.80 : 0.65)
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

        char performanceText[220];

        snprintf(
            performanceText,
            sizeof(performanceText),
            "Display %.1f FPS | Detect %.1f ms | Source %.0f ms | Done %.0f ms | Confirm %d | Alarm %.0f/%.0f ms | Raw %d/%d | Risk %s",
            averageDisplayFps,
            averageDetectMs,
            resultAgeMs,
            completedAgeMs,
            displayResult.confirmCount,
            pendingFireMs,
            activeConfirmMs,
            rawFireResultCount,
            activeMinRawFireResults,
            activeAmbiguousWarmObject ? "WARM" : "NORMAL"
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