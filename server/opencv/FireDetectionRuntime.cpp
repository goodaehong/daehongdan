#include "FireDetectionRuntime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cmath>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

#include "AppConfig.h"
#include "FlameDetector.h"

using std::lock_guard;
using std::mutex;
using std::unique_lock;

namespace
{
    constexpr std::size_t FIRE_POSITION_HISTORY_LIMIT = 32;
    constexpr std::int64_t FIRE_POSITION_HISTORY_RETENTION_MS = 5000;
    constexpr std::int64_t FIRE_MOTION_WINDOW_MS = 3000;
    constexpr std::int64_t FIRE_MOTION_MIN_WINDOW_MS = 1500;
    constexpr std::size_t FIRE_MOTION_MIN_SAMPLES = 8;
    constexpr std::size_t FIRE_MOTION_SMOOTHING_GROUPS = 4;
    constexpr double FIRE_MOTION_MIN_DISTANCE = 0.030;
    constexpr double FIRE_MOTION_MAX_ADAPTIVE_DISTANCE = 0.060;
    constexpr double FIRE_MOTION_BOX_DISTANCE_RATIO = 0.12;
    constexpr double FIRE_MOTION_MIN_COHERENCE = 0.80;
    constexpr int FIRE_MOTION_CONFIRM_RESULTS = 3;
    constexpr double FIRE_MOTION_MIN_DIRECTION_DOT = 0.85;

    struct FireTrackHistory
    {
        std::deque<TrackPositionSample> samples;
        std::deque<TrackPositionSample> motionSamples;
        std::int64_t lastSeenTimestampMs = 0;
        int motionCandidateHits = 0;
        double candidateDirectionX = 0.0;
        double candidateDirectionY = 0.0;
        bool motionConfirmed = false;
        double confirmedDirectionX = 0.0;
        double confirmedDirectionY = 0.0;
        double confirmedSpeed = 0.0;
        std::int64_t confirmedWindowMs = 0;
    };

    class CameraHealthMonitor
    {
    public:
        CameraHealthStatus analyze(const cv::Mat& frame, std::int64_t timestampMs)
        {
            if (frame.empty())
                return latestStatus_;
            if (lastAnalysisTimestampMs_ != 0 &&
                timestampMs >= lastAnalysisTimestampMs_ &&
                timestampMs - lastAnalysisTimestampMs_ <
                    camera_health_config::ANALYSIS_INTERVAL_MS)
                return latestStatus_;

            lastAnalysisTimestampMs_ = timestampMs;
            CameraHealthStatus status;

            cv::Mat analysisFrame;
            if (frame.cols > camera_health_config::ANALYSIS_WIDTH)
            {
                const double scale = static_cast<double>(camera_health_config::ANALYSIS_WIDTH) /
                    static_cast<double>(frame.cols);
                cv::resize(frame, analysisFrame, cv::Size(), scale, scale, cv::INTER_AREA);
            }
            else
            {
                analysisFrame = frame;
            }

            cv::Mat gray;
            if (analysisFrame.channels() == 1)
                gray = analysisFrame;
            else if (analysisFrame.channels() == 4)
                cv::cvtColor(analysisFrame, gray, cv::COLOR_BGRA2GRAY);
            else
                cv::cvtColor(analysisFrame, gray, cv::COLOR_BGR2GRAY);

            cv::Mat laplacian;
            cv::Laplacian(gray, laplacian, CV_16S);
            cv::Scalar laplacianMean;
            cv::Scalar laplacianStdDev;
            cv::meanStdDev(laplacian, laplacianMean, laplacianStdDev);

            const double pixelCount = static_cast<double>(gray.total());
            status.valid = pixelCount > 0.0;
            status.laplacianVariance =
                laplacianStdDev[0] * laplacianStdDev[0];
            status.darkPixelRatio = pixelCount > 0.0 ?
                static_cast<double>(cv::countNonZero(
                    gray < camera_health_config::DARK_PIXEL_THRESHOLD)) / pixelCount : 0.0;
            status.brightPixelRatio = pixelCount > 0.0 ?
                static_cast<double>(cv::countNonZero(
                    gray > camera_health_config::BRIGHT_PIXEL_THRESHOLD)) / pixelCount : 0.0;

            const bool obstruction =
                status.darkPixelRatio >= camera_health_config::DARK_PIXEL_RATIO_THRESHOLD ||
                status.brightPixelRatio >= camera_health_config::BRIGHT_PIXEL_RATIO_THRESHOLD;
            const CameraHealthIssue observedIssue = obstruction ?
                CameraHealthIssue::OBSTRUCTION :
                (status.laplacianVariance <
                    camera_health_config::BLUR_LAPLACIAN_VARIANCE_THRESHOLD ?
                    CameraHealthIssue::BLUR : CameraHealthIssue::NONE);

            updateState(observedIssue, timestampMs);
            status.contaminationSuspected = activeIssue_ != CameraHealthIssue::NONE;
            status.issue = activeIssue_;
            status.consecutiveSuspectFrames = consecutiveSuspectFrames_;
            latestStatus_ = status;
            return latestStatus_;
        }

        void reset()
        {
            pendingIssue_ = CameraHealthIssue::NONE;
            activeIssue_ = CameraHealthIssue::NONE;
            pendingSinceMs_ = 0;
            healthySinceMs_ = 0;
            consecutiveSuspectFrames_ = 0;
            lastAnalysisTimestampMs_ = 0;
            latestStatus_ = CameraHealthStatus{};
        }

    private:
        void updateState(CameraHealthIssue observedIssue, std::int64_t timestampMs)
        {
            if (observedIssue == CameraHealthIssue::NONE)
            {
                pendingIssue_ = CameraHealthIssue::NONE;
                pendingSinceMs_ = 0;
                consecutiveSuspectFrames_ = 0;

                if (activeIssue_ != CameraHealthIssue::NONE)
                {
                    if (healthySinceMs_ == 0)
                        healthySinceMs_ = timestampMs;
                    else if (timestampMs - healthySinceMs_ >=
                        camera_health_config::HEALTHY_CLEAR_MS)
                        activeIssue_ = CameraHealthIssue::NONE;
                }
                else
                {
                    healthySinceMs_ = 0;
                }
                return;
            }

            healthySinceMs_ = 0;
            if (pendingIssue_ != observedIssue)
            {
                pendingIssue_ = observedIssue;
                pendingSinceMs_ = timestampMs;
                consecutiveSuspectFrames_ = 1;
            }
            else
            {
                ++consecutiveSuspectFrames_;
            }

            if (timestampMs - pendingSinceMs_ >=
                camera_health_config::WARNING_PERSISTENCE_MS)
                activeIssue_ = pendingIssue_;
        }

        CameraHealthIssue pendingIssue_ = CameraHealthIssue::NONE;
        CameraHealthIssue activeIssue_ = CameraHealthIssue::NONE;
        std::int64_t pendingSinceMs_ = 0;
        std::int64_t healthySinceMs_ = 0;
        std::int64_t lastAnalysisTimestampMs_ = 0;
        int consecutiveSuspectFrames_ = 0;
        CameraHealthStatus latestStatus_;
    };

    std::atomic<int> gRuntimeInstanceCounter{ 0 };
    std::mutex gFireDetectorExecutionMutex;

    // 네 채널의 제출 시점을 분산해 같은 순간에 CPU 작업이 몰리지 않게 한다.
    int nextRuntimePhaseMs()
    {
        constexpr int CHANNEL_PHASE_COUNT = 4;
        const int index = gRuntimeInstanceCounter.fetch_add(1) % CHANNEL_PHASE_COUNT;
        return index * flame_config::DETECTION_INTERVAL_MS / CHANNEL_PHASE_COUNT;
    }

    double clampValue(double value, double minimum, double maximum)
    {
        return std::max(minimum, std::min(value, maximum));
    }
}

class FireDetectionRuntime::Impl
{
public:
    Impl()
        : submitPhaseOffsetMs_(nextRuntimePhaseMs()),
        nextAcceptedSubmitTime_(Clock::now() + std::chrono::milliseconds(submitPhaseOffsetMs_)),
        workerThread_(&Impl::workerLoop, this)
    {
    }
    ~Impl() { stop(); }

    void submitFrame(const cv::Mat& frame, std::uint64_t frameId, TimePoint sourceTime)
    {
        if (frame.empty() || !running_.load()) return;
        {
            lock_guard<mutex> lock(jobMutex_);

            // 호출자가 30 FPS로 제출해도 채널별 분석률을 설정값으로 제한한다.
            if (sourceTime < nextAcceptedSubmitTime_)
                return;

            nextAcceptedSubmitTime_ =
                sourceTime + std::chrono::milliseconds(
                    flame_config::DETECTION_INTERVAL_MS
                );

            frame.copyTo(pendingFrame_);
            pendingFrameId_ = frameId;
            pendingEpoch_ = streamEpoch_.load();
            pendingSourceTime_ = sourceTime;
            jobReady_ = true;
        }
        jobCondition_.notify_one();
    }

    void setIgnoreRegionConfig(const IgnoreRegionConfig& config)
    {
        {
            lock_guard<mutex> lock(ignoreConfigMutex_);
            pendingIgnoreConfig_ = config;
            pendingIgnoreConfigVersion_ = ignoreConfigVersion_.fetch_add(1) + 1;
        }

        // 새 ROI가 적용되기 전에 게시된 결과와 경보 누적을 즉시 폐기한다.
        {
            lock_guard<mutex> lock(resultMutex_);
            latestResult_ = DetectionResult{};
            latestCameraHealth_ = CameraHealthStatus{};
            latestResultFrameId_ = 0;
            hasResult_ = false;
        }
        {
            lock_guard<mutex> lock(stateMutex_);
            alarmController_.reset();
            lastAlarmProcessedFrameId_ = 0;
        }
    }

    void resetStream()
    {
        // epoch가 다른 진행 중 결과는 작업 완료 후에도 게시되지 않는다.
        streamEpoch_.fetch_add(1);
        detectorResetRequested_ = true;

        {
            lock_guard<mutex> lock(jobMutex_);
            pendingFrame_.release();
            pendingFrameId_ = 0;
            pendingEpoch_ = streamEpoch_.load();
            nextAcceptedSubmitTime_ = Clock::now() +
                std::chrono::milliseconds(submitPhaseOffsetMs_);
            jobReady_ = false;
        }
        {
            lock_guard<mutex> lock(resultMutex_);
            latestResult_ = DetectionResult{};
            latestCameraHealth_ = CameraHealthStatus{};
            latestResultFrameId_ = 0;
            latestResultEpoch_ = streamEpoch_.load();
            latestDetectMs_ = 0.0;
            hasResult_ = false;
        }
        {
            lock_guard<mutex> lock(stateMutex_);
            alarmController_.reset();
            lastAlarmProcessedFrameId_ = 0;
            lastAverageProcessedFrameId_ = 0;
            averageDetectMs_ = 0.0;
        }
    }

    FireRuntimeSnapshot poll(TimePoint now)
    {
        FireRuntimeSnapshot snapshot;
        std::uint64_t resultEpoch = 0;
        TimePoint sourceTime, completedTime;

        {
            lock_guard<mutex> lock(resultMutex_);
            snapshot.detection = latestResult_;
            snapshot.cameraHealth = latestCameraHealth_;
            snapshot.resultFrameId = latestResultFrameId_;
            resultEpoch = latestResultEpoch_;
            snapshot.detectMs = latestDetectMs_;
            sourceTime = latestSourceTime_;
            completedTime = latestCompletedTime_;
            snapshot.hasResult = hasResult_ && resultEpoch == streamEpoch_.load();
        }

        lock_guard<mutex> stateLock(stateMutex_);
        if (snapshot.hasResult && snapshot.detectMs > 0.0 &&
            snapshot.resultFrameId != lastAverageProcessedFrameId_)
        {
            averageDetectMs_ = averageDetectMs_ <= 0.0 ? snapshot.detectMs :
                averageDetectMs_ * 0.90 + snapshot.detectMs * 0.10;
            lastAverageProcessedFrameId_ = snapshot.resultFrameId;
        }
        snapshot.averageDetectMs = averageDetectMs_;

        if (snapshot.hasResult)
        {
            snapshot.resultAgeMs = std::chrono::duration<double, std::milli>(now - sourceTime).count();
            snapshot.completedAgeMs = std::chrono::duration<double, std::milli>(now - completedTime).count();
        }

        // 느린 장치에서는 실제 평균 처리 시간에 맞춰 결과 유효시간을 자동 보정한다.
        snapshot.resultFreshLimitMs = clampValue(
            averageDetectMs_ * 2.2 + 300.0,
            1000.0,
            2500.0
        );
        snapshot.boxFreshLimitMs = clampValue(
            averageDetectMs_ * 1.6 + 120.0,
            300.0,
            1200.0
        );
        snapshot.resultIsFresh = snapshot.hasResult && snapshot.resultAgeMs <= snapshot.resultFreshLimitMs;

        const bool newResult = snapshot.hasResult && snapshot.resultFrameId != 0 &&
            snapshot.resultFrameId != lastAlarmProcessedFrameId_;
        if (newResult)
        {
            lastAlarmProcessedFrameId_ = snapshot.resultFrameId;
            snapshot.alarm = alarmController_.processNewResult(
                snapshot.detection, snapshot.resultIsFresh, now);
        }
        else
        {
            snapshot.alarm = alarmController_.tick(snapshot.resultIsFresh, now);
        }

        snapshot.boxIsFresh = snapshot.alarm.alarmActive && snapshot.resultIsFresh &&
            snapshot.detection.detected && snapshot.resultAgeMs <= snapshot.boxFreshLimitMs &&
            !snapshot.detection.boxes.empty();
        return snapshot;
    }

    void stop()
    {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) return;
        {
            lock_guard<mutex> lock(jobMutex_);
            jobReady_ = false;
        }
        jobCondition_.notify_one();
        if (workerThread_.joinable()) workerThread_.join();
    }

private:
    std::uint64_t applyPendingIgnoreConfig()
    {
        lock_guard<mutex> lock(ignoreConfigMutex_);
        if (appliedIgnoreConfigVersion_ != pendingIgnoreConfigVersion_)
        {
            detector_.setIgnoreRegionConfig(pendingIgnoreConfig_);
            fireTrackHistories_.clear();
            appliedIgnoreConfigVersion_ = pendingIgnoreConfigVersion_;
        }
        return appliedIgnoreConfigVersion_;
    }

    void attachFirePositionHistory(
        DetectionResult& detection,
        std::uint64_t frameId,
        TimePoint sourceTime)
    {
        const std::int64_t timestampMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                sourceTime.time_since_epoch()).count();

        for (DetectionBox& box : detection.boxes)
        {
            if (box.type != DetectionType::FIRE || box.trackId < 0 ||
                !box.representativePositionValid)
                continue;

            FireTrackHistory& history = fireTrackHistories_[box.trackId];
            history.lastSeenTimestampMs = timestampMs;

            // 검출기가 한 프레임을 놓쳐 유지 중인 박스는 새 위치 측정값이 아니므로
            // 같은 좌표를 이력에 중복 추가하지 않는다.
            bool positionSampleAdded = false;
            if (!box.trackedPersistenceEvidence &&
                (history.samples.empty() || history.samples.back().frameId != frameId))
            {
                TrackPositionSample sample;
                sample.frameId = frameId;
                sample.timestampMs = timestampMs;
                sample.normalizedX = box.representativeNormalizedX;
                sample.normalizedY = box.representativeNormalizedY;
                history.samples.push_back(sample);

                // The bottom-center point is kept for floor-map positioning. Motion uses
                // the box center separately so height flicker does not directly look like
                // vertical translation.
                TrackPositionSample motionSample = sample;
                motionSample.normalizedX = std::clamp(
                    box.normalizedX + box.normalizedWidth * 0.5, 0.0, 1.0);
                motionSample.normalizedY = std::clamp(
                    box.normalizedY + box.normalizedHeight * 0.5, 0.0, 1.0);
                history.motionSamples.push_back(motionSample);
                positionSampleAdded = true;
                while (history.samples.size() > FIRE_POSITION_HISTORY_LIMIT)
                    history.samples.pop_front();
                while (history.motionSamples.size() > FIRE_POSITION_HISTORY_LIMIT)
                    history.motionSamples.pop_front();
            }

            box.positionHistory.assign(history.samples.begin(), history.samples.end());

            std::vector<const TrackPositionSample*> motionSamples;
            if (!history.motionSamples.empty())
            {
                const TrackPositionSample& latest = history.motionSamples.back();
                for (const TrackPositionSample& sample : history.motionSamples)
                {
                    if (latest.timestampMs - sample.timestampMs <= FIRE_MOTION_WINDOW_MS)
                        motionSamples.push_back(&sample);
                }
            }

            if (positionSampleAdded &&
                motionSamples.size() >= FIRE_MOTION_MIN_SAMPLES)
            {
                struct SmoothedPoint
                {
                    double x = 0.0;
                    double y = 0.0;
                    double timestampMs = 0.0;
                };

                std::vector<SmoothedPoint> smoothed;
                smoothed.reserve(FIRE_MOTION_SMOOTHING_GROUPS);
                for (std::size_t group = 0;
                    group < FIRE_MOTION_SMOOTHING_GROUPS; ++group)
                {
                    const std::size_t begin =
                        group * motionSamples.size() / FIRE_MOTION_SMOOTHING_GROUPS;
                    const std::size_t end =
                        (group + 1) * motionSamples.size() /
                        FIRE_MOTION_SMOOTHING_GROUPS;
                    if (end <= begin)
                        continue;

                    SmoothedPoint point;
                    for (std::size_t index = begin; index < end; ++index)
                    {
                        point.x += motionSamples[index]->normalizedX;
                        point.y += motionSamples[index]->normalizedY;
                        point.timestampMs +=
                            static_cast<double>(motionSamples[index]->timestampMs);
                    }
                    const double count = static_cast<double>(end - begin);
                    point.x /= count;
                    point.y /= count;
                    point.timestampMs /= count;
                    smoothed.push_back(point);
                }

                if (smoothed.size() == FIRE_MOTION_SMOOTHING_GROUPS)
                {
                    const SmoothedPoint& first = smoothed.front();
                    const SmoothedPoint& last = smoothed.back();
                    const std::int64_t elapsedMs = static_cast<std::int64_t>(
                        std::llround(last.timestampMs - first.timestampMs));
                    const double deltaX = last.x - first.x;
                    const double deltaY = last.y - first.y;
                    const double netDistance = std::hypot(deltaX, deltaY);
                    double pathDistance = 0.0;
                    for (std::size_t index = 1; index < smoothed.size(); ++index)
                    {
                        pathDistance += std::hypot(
                            smoothed[index].x - smoothed[index - 1].x,
                            smoothed[index].y - smoothed[index - 1].y);
                    }

                    const double boxDiagonal = std::hypot(
                        box.normalizedWidth, box.normalizedHeight);
                    const double requiredDistance = std::clamp(
                        boxDiagonal * FIRE_MOTION_BOX_DISTANCE_RATIO,
                        FIRE_MOTION_MIN_DISTANCE,
                        FIRE_MOTION_MAX_ADAPTIVE_DISTANCE);
                    const double coherence = pathDistance > 0.0 ?
                        netDistance / pathDistance : 0.0;

                    if (elapsedMs >= FIRE_MOTION_MIN_WINDOW_MS &&
                        netDistance >= requiredDistance &&
                        coherence >= FIRE_MOTION_MIN_COHERENCE)
                    {
                        const double directionX = deltaX / netDistance;
                        const double directionY = deltaY / netDistance;
                        const double directionDot =
                            directionX * history.candidateDirectionX +
                            directionY * history.candidateDirectionY;
                        if (history.motionCandidateHits > 0 &&
                            directionDot >= FIRE_MOTION_MIN_DIRECTION_DOT)
                            ++history.motionCandidateHits;
                        else
                            history.motionCandidateHits = 1;

                        history.candidateDirectionX = directionX;
                        history.candidateDirectionY = directionY;
                        if (history.motionCandidateHits >= FIRE_MOTION_CONFIRM_RESULTS)
                        {
                            history.motionConfirmed = true;
                            history.confirmedDirectionX = directionX;
                            history.confirmedDirectionY = directionY;
                            history.confirmedSpeed =
                                netDistance * 1000.0 / static_cast<double>(elapsedMs);
                            history.confirmedWindowMs = elapsedMs;
                        }
                        else
                        {
                            history.motionConfirmed = false;
                        }
                    }
                    else
                    {
                        history.motionCandidateHits = 0;
                        history.candidateDirectionX = 0.0;
                        history.candidateDirectionY = 0.0;
                        history.motionConfirmed = false;
                    }
                }
            }
            else if (positionSampleAdded)
            {
                history.motionCandidateHits = 0;
                history.motionConfirmed = false;
            }

            box.imageMotionValid = history.motionConfirmed &&
                !box.trackedPersistenceEvidence;
            if (box.imageMotionValid)
            {
                box.imageDirectionX = history.confirmedDirectionX;
                box.imageDirectionY = history.confirmedDirectionY;
                box.imageSpeedNormalizedPerSecond = history.confirmedSpeed;
                box.imageMotionWindowMs = history.confirmedWindowMs;
            }
        }

        for (auto it = fireTrackHistories_.begin(); it != fireTrackHistories_.end();)
        {
            if (timestampMs - it->second.lastSeenTimestampMs >
                FIRE_POSITION_HISTORY_RETENTION_MS)
                it = fireTrackHistories_.erase(it);
            else
                ++it;
        }
    }

    void workerLoop()
    {
        while (true)
        {
            cv::Mat frame;
            std::uint64_t frameId = 0;
            std::uint64_t frameEpoch = 0;
            TimePoint sourceTime;

            {
                unique_lock<mutex> lock(jobMutex_);
                jobCondition_.wait(lock, [&] { return !running_.load() || jobReady_; });
                if (!running_.load() && !jobReady_) break;
                frame = pendingFrame_;
                frameId = pendingFrameId_;
                frameEpoch = pendingEpoch_;
                sourceTime = pendingSourceTime_;
                jobReady_ = false;
            }

            if (frame.empty()) continue;
            if (detectorResetRequested_.exchange(false))
            {
                detector_.reset();
                fireTrackHistories_.clear();
                cameraHealthMonitor_.reset();
            }

            const std::uint64_t ignoreConfigVersion = applyPendingIgnoreConfig();

            const TimePoint start = Clock::now();
            const std::int64_t sourceTimestampMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    sourceTime.time_since_epoch()).count();
            DetectionResult detection;
            {
                // 네 채널 런타임은 독립적이지만 OpenCV 화염 분석은 한 번에 하나만 실행한다.
                // Raspberry Pi 4에서 채널 간 CPU 과다 경쟁이 생기는 것을 막기 위함이다.
                lock_guard<mutex> detectorLock(gFireDetectorExecutionMutex);
                detection = detector_.detect(frame, frameId, sourceTimestampMs);
            }
            const double detectMs =
                std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            const CameraHealthStatus cameraHealth =
                cameraHealthMonitor_.analyze(frame, sourceTimestampMs);
            if (frameEpoch != streamEpoch_.load() ||
                ignoreConfigVersion != ignoreConfigVersion_.load())
                continue;

            attachFirePositionHistory(detection, frameId, sourceTime);

            lock_guard<mutex> lock(resultMutex_);
            latestResult_ = std::move(detection);
            latestCameraHealth_ = cameraHealth;
            latestResultFrameId_ = frameId;
            latestResultEpoch_ = frameEpoch;
            latestDetectMs_ = detectMs;
            latestSourceTime_ = sourceTime;
            latestCompletedTime_ = Clock::now();
            hasResult_ = true;
        }
    }

    FlameDetector detector_;
    CameraHealthMonitor cameraHealthMonitor_;
    std::unordered_map<int, FireTrackHistory> fireTrackHistories_;
    FireAlarmController alarmController_;
    int submitPhaseOffsetMs_ = 0;
    TimePoint nextAcceptedSubmitTime_{};
    std::atomic<bool> running_{ true };
    std::atomic<bool> detectorResetRequested_{ false };
    std::atomic<std::uint64_t> streamEpoch_{ 0 };
    std::atomic<std::uint64_t> ignoreConfigVersion_{ 0 };
    std::thread workerThread_;

    mutex ignoreConfigMutex_;
    IgnoreRegionConfig pendingIgnoreConfig_;
    std::uint64_t pendingIgnoreConfigVersion_ = 0;
    std::uint64_t appliedIgnoreConfigVersion_ = 0;

    mutex jobMutex_;
    std::condition_variable jobCondition_;
    cv::Mat pendingFrame_;
    std::uint64_t pendingFrameId_ = 0;
    std::uint64_t pendingEpoch_ = 0;
    TimePoint pendingSourceTime_;
    bool jobReady_ = false;

    mutex resultMutex_;
    DetectionResult latestResult_;
    CameraHealthStatus latestCameraHealth_;
    std::uint64_t latestResultFrameId_ = 0;
    std::uint64_t latestResultEpoch_ = 0;
    double latestDetectMs_ = 0.0;
    TimePoint latestSourceTime_;
    TimePoint latestCompletedTime_;
    bool hasResult_ = false;

    mutex stateMutex_;
    std::uint64_t lastAlarmProcessedFrameId_ = 0;
    std::uint64_t lastAverageProcessedFrameId_ = 0;
    double averageDetectMs_ = 0.0;
};

FireDetectionRuntime::FireDetectionRuntime() : impl_(new Impl()) {}
FireDetectionRuntime::~FireDetectionRuntime() = default;

void FireDetectionRuntime::submitFrame(const cv::Mat& frame, std::uint64_t frameId, TimePoint sourceTime)
{
    impl_->submitFrame(frame, frameId, sourceTime);
}

void FireDetectionRuntime::setIgnoreRegionConfig(const IgnoreRegionConfig& config)
{
    impl_->setIgnoreRegionConfig(config);
}

void FireDetectionRuntime::resetStream() { impl_->resetStream(); }
FireRuntimeSnapshot FireDetectionRuntime::poll(TimePoint now) { return impl_->poll(now); }
void FireDetectionRuntime::stop() { impl_->stop(); }
