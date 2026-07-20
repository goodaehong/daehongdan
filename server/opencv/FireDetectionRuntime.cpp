#include "FireDetectionRuntime.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

#include "FireDetector_1.h"

using std::lock_guard;
using std::mutex;
using std::unique_lock;

namespace
{
    double clampValue(double value, double minimum, double maximum)
    {
        return std::max(minimum, std::min(value, maximum));
    }
}

class FireDetectionRuntime::Impl
{
public:
    Impl() : workerThread_(&Impl::workerLoop, this) {}
    ~Impl() { stop(); }

    void submitFrame(const cv::Mat& frame, std::uint64_t frameId, TimePoint sourceTime)
    {
        if (frame.empty() || !running_.load()) return;
        {
            lock_guard<mutex> lock(jobMutex_);
            pendingFrame_ = frame;
            pendingFrameId_ = frameId;
            pendingEpoch_ = streamEpoch_.load();
            pendingSourceTime_ = sourceTime;
            jobReady_ = true;
        }
        jobCondition_.notify_one();
    }

    void resetStream()
    {
        streamEpoch_.fetch_add(1);
        detectorResetRequested_ = true;

        {
            lock_guard<mutex> lock(jobMutex_);
            pendingFrame_.release();
            pendingFrameId_ = 0;
            pendingEpoch_ = streamEpoch_.load();
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
            if (detectorResetRequested_.exchange(false)) detector_.reset();

            const TimePoint start = Clock::now();
            DetectionResult detection = detector_.detect(frame);
            const double detectMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            if (frameEpoch != streamEpoch_.load()) continue;

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

    FireDetector detector_;
    FireAlarmController alarmController_;
    std::atomic<bool> running_{ true };
    std::atomic<bool> detectorResetRequested_{ false };
    std::atomic<std::uint64_t> streamEpoch_{ 0 };
    std::thread workerThread_;

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

void FireDetectionRuntime::resetStream() { impl_->resetStream(); }
FireRuntimeSnapshot FireDetectionRuntime::poll(TimePoint now) { return impl_->poll(now); }
void FireDetectionRuntime::stop() { impl_->stop(); }