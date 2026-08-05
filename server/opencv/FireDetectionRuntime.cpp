#include "FireDetectionRuntime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
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

    struct FireTrackHistory
    {
        std::deque<TrackPositionSample> samples;
        std::int64_t lastSeenTimestampMs = 0;
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
            if (!box.trackedPersistenceEvidence &&
                (history.samples.empty() || history.samples.back().frameId != frameId))
            {
                TrackPositionSample sample;
                sample.frameId = frameId;
                sample.timestampMs = timestampMs;
                sample.normalizedX = box.representativeNormalizedX;
                sample.normalizedY = box.representativeNormalizedY;
                history.samples.push_back(sample);
                while (history.samples.size() > FIRE_POSITION_HISTORY_LIMIT)
                    history.samples.pop_front();
            }

            box.positionHistory.assign(history.samples.begin(), history.samples.end());
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
            }

            const std::uint64_t ignoreConfigVersion = applyPendingIgnoreConfig();

            const TimePoint start = Clock::now();
            DetectionResult detection;
            {
                // 네 채널 런타임은 독립적이지만 OpenCV 화염 분석은 한 번에 하나만 실행한다.
                // Raspberry Pi 4에서 채널 간 CPU 과다 경쟁이 생기는 것을 막기 위함이다.
                lock_guard<mutex> detectorLock(gFireDetectorExecutionMutex);
                detection = detector_.detect(frame);
            }
            const double detectMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (frameEpoch != streamEpoch_.load() ||
                ignoreConfigVersion != ignoreConfigVersion_.load())
                continue;

            attachFirePositionHistory(detection, frameId, sourceTime);

            lock_guard<mutex> lock(resultMutex_);
            latestResult_ = std::move(detection);
            latestResultFrameId_ = frameId;
            latestResultEpoch_ = frameEpoch;
            latestDetectMs_ = detectMs;
            latestSourceTime_ = sourceTime;
            latestCompletedTime_ = Clock::now();
            hasResult_ = true;
        }
    }

    FlameDetector detector_;
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
