#include "SmokeDetectionRuntime.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "AppConfig.h"
#include "SmokeDetector.h"

namespace
{
    // 움직임 검증은 NCNN 입력과 별도로 작은 회색조 프레임에서 계산한다.
    cv::Mat makeMotionGray(const cv::Mat& frame)
    {
        if (frame.empty()) return {};

        cv::Mat gray;
        if (frame.channels() == 1)
            gray = frame;
        else if (frame.channels() == 4)
            cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
        else
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

        const int width = std::min(smoke_config::MOTION_ANALYSIS_WIDTH, gray.cols);
        const int height = std::max(1, static_cast<int>(std::round(
            static_cast<double>(gray.rows) * width / std::max(1, gray.cols))));

        cv::Mat resized;
        if (gray.cols != width || gray.rows != height)
            cv::resize(gray, resized, cv::Size(width, height), 0.0, 0.0, cv::INTER_AREA);
        else
            resized = gray.clone();

        cv::GaussianBlur(resized, resized, cv::Size(5, 5), 0.0);
        return resized;
    }

    cv::Rect scaleBox(
        const cv::Rect& box,
        const cv::Size& sourceSize,
        const cv::Size& targetSize)
    {
        if (sourceSize.width <= 0 || sourceSize.height <= 0) return {};

        const double scaleX =
            static_cast<double>(targetSize.width) / static_cast<double>(sourceSize.width);
        const double scaleY =
            static_cast<double>(targetSize.height) / static_cast<double>(sourceSize.height);
        const int x1 = static_cast<int>(std::floor(box.x * scaleX));
        const int y1 = static_cast<int>(std::floor(box.y * scaleY));
        const int x2 = static_cast<int>(std::ceil((box.x + box.width) * scaleX));
        const int y2 = static_cast<int>(std::ceil((box.y + box.height) * scaleY));
        return cv::Rect(x1, y1, x2 - x1, y2 - y1) &
            cv::Rect(0, 0, targetSize.width, targetSize.height);
    }

    double intersectionOverUnion(const cv::Rect& a, const cv::Rect& b)
    {
        const cv::Rect intersection = a & b;
        if (intersection.empty()) return 0.0;

        const double intersectionArea = static_cast<double>(intersection.area());
        const double unionArea =
            static_cast<double>(a.area()) + static_cast<double>(b.area()) -
            intersectionArea;
        return unionArea > 0.0 ? intersectionArea / unionArea : 0.0;
    }

    bool belongsToTrackedRegion(const cv::Rect& previous, const cv::Rect& current)
    {
        if (previous.empty() || current.empty()) return false;
        if (intersectionOverUnion(previous, current) >= smoke_config::TRACK_MIN_IOU)
            return true;

        const double previousCenterX = previous.x + previous.width * 0.5;
        const double previousCenterY = previous.y + previous.height * 0.5;
        const double currentCenterX = current.x + current.width * 0.5;
        const double currentCenterY = current.y + current.height * 0.5;
        const double centerDistance = std::hypot(
            currentCenterX - previousCenterX,
            currentCenterY - previousCenterY);
        const double referenceSize = static_cast<double>(std::max({
            previous.width, previous.height, current.width, current.height, 1 }));
        return centerDistance <=
            referenceSize * smoke_config::TRACK_MAX_CENTER_DISTANCE_RATIO;
    }

    cv::Rect expandedForSmokeMerge(const cv::Rect& box)
    {
        const int marginX = std::max(1, static_cast<int>(std::round(
            box.width * smoke_config::MERGE_EXPANSION_RATIO)));
        const int marginY = std::max(1, static_cast<int>(std::round(
            box.height * smoke_config::MERGE_EXPANSION_RATIO)));
        return cv::Rect(
            box.x - marginX,
            box.y - marginY,
            box.width + marginX * 2,
            box.height + marginY * 2);
    }

    bool shouldMergeSmokeBoxes(const cv::Rect& left, const cv::Rect& right)
    {
        if (left.empty() || right.empty()) return false;
        return !(expandedForSmokeMerge(left) & expandedForSmokeMerge(right)).empty();
    }

    void mergeNearbySmokeBoxes(std::vector<DetectionBox>& boxes)
    {
        bool merged = true;
        while (merged)
        {
            merged = false;
            for (std::size_t left = 0; left < boxes.size() && !merged; ++left)
            {
                for (std::size_t right = left + 1; right < boxes.size(); ++right)
                {
                    if (!shouldMergeSmokeBoxes(boxes[left].box, boxes[right].box))
                        continue;

                    boxes[left].box |= boxes[right].box;
                    boxes[left].score = std::max(boxes[left].score, boxes[right].score);
                    boxes.erase(boxes.begin() + static_cast<std::ptrdiff_t>(right));
                    merged = true;
                    break;
                }
            }
        }
    }

    void setNormalizedBoxGeometry(DetectionBox& box, const cv::Size& frameSize)
    {
        if (frameSize.width <= 0 || frameSize.height <= 0 || box.box.empty()) return;
        box.normalizedX = std::clamp(
            static_cast<double>(box.box.x) / frameSize.width, 0.0, 1.0);
        box.normalizedY = std::clamp(
            static_cast<double>(box.box.y) / frameSize.height, 0.0, 1.0);
        box.normalizedWidth = std::clamp(
            static_cast<double>(box.box.width) / frameSize.width, 0.0, 1.0);
        box.normalizedHeight = std::clamp(
            static_cast<double>(box.box.height) / frameSize.height, 0.0, 1.0);
    }

    struct MotionEvidence
    {
        double localRatio = 0.0;
        double innerRatio = 0.0;
        int activeCells = 0;
        bool verified = false;
    };

    // 후보 박스 전체·내부·격자에서 움직임이 퍼져 있는지 측정한다.
    MotionEvidence analyzeMotion(
        const cv::Mat& motionMask,
        const cv::Rect& motionBox)
    {
        MotionEvidence evidence;
        if (motionMask.empty() || motionBox.empty()) return evidence;

        const cv::Mat localMask = motionMask(motionBox);
        evidence.localRatio =
            static_cast<double>(cv::countNonZero(localMask)) /
            static_cast<double>(localMask.total());

        const int marginX = std::min(
            motionBox.width / 3,
            static_cast<int>(std::round(
                motionBox.width * smoke_config::MOTION_INNER_MARGIN_RATIO)));
        const int marginY = std::min(
            motionBox.height / 3,
            static_cast<int>(std::round(
                motionBox.height * smoke_config::MOTION_INNER_MARGIN_RATIO)));
        const cv::Rect innerBox(
            marginX,
            marginY,
            std::max(1, motionBox.width - marginX * 2),
            std::max(1, motionBox.height - marginY * 2));
        const cv::Mat innerMask = localMask(innerBox);
        evidence.innerRatio =
            static_cast<double>(cv::countNonZero(innerMask)) /
            static_cast<double>(innerMask.total());

        for (int row = 0; row < smoke_config::MOTION_GRID_ROWS; ++row)
        {
            const int y1 = localMask.rows * row / smoke_config::MOTION_GRID_ROWS;
            const int y2 =
                localMask.rows * (row + 1) / smoke_config::MOTION_GRID_ROWS;
            for (int column = 0; column < smoke_config::MOTION_GRID_COLUMNS; ++column)
            {
                const int x1 =
                    localMask.cols * column / smoke_config::MOTION_GRID_COLUMNS;
                const int x2 =
                    localMask.cols * (column + 1) / smoke_config::MOTION_GRID_COLUMNS;
                const cv::Rect cell(x1, y1, x2 - x1, y2 - y1);
                if (cell.empty()) continue;

                const cv::Mat cellMask = localMask(cell);
                const double cellRatio =
                    static_cast<double>(cv::countNonZero(cellMask)) /
                    static_cast<double>(cellMask.total());
                if (cellRatio >= smoke_config::MOTION_MIN_CELL_RATIO)
                    ++evidence.activeCells;
            }
        }

        evidence.verified =
            evidence.localRatio >= smoke_config::MOTION_MIN_RATIO &&
            evidence.localRatio <= smoke_config::MOTION_MAX_VALID_RATIO &&
            evidence.innerRatio >= smoke_config::MOTION_MIN_INNER_RATIO &&
            evidence.activeCells >= smoke_config::MOTION_MIN_ACTIVE_CELLS;
        return evidence;
    }

    void applyMotionScore(
        const cv::Mat& frame,
        const cv::Mat& previousGray,
        SmokeDetectionResult& detection,
        cv::Mat& currentGray)
    {
        currentGray = makeMotionGray(frame);
        const bool hasHistory = !previousGray.empty() && !currentGray.empty() &&
            previousGray.size() == currentGray.size();

        cv::Mat motionMask;
        double globalMotionRatio = 0.0;
        if (hasHistory)
        {
            // 연속 추론 프레임의 절대 차이에서 작은 노이즈를 제거한다.
            cv::absdiff(previousGray, currentGray, motionMask);
            cv::threshold(
                motionMask, motionMask, smoke_config::MOTION_PIXEL_THRESHOLD,
                255, cv::THRESH_BINARY);
            const cv::Mat kernel = cv::getStructuringElement(
                cv::MORPH_ELLIPSE, cv::Size(3, 3));
            cv::morphologyEx(motionMask, motionMask, cv::MORPH_OPEN, kernel);
            globalMotionRatio =
                static_cast<double>(cv::countNonZero(motionMask)) /
                static_cast<double>(motionMask.total());
        }

        double bestAdjustedScore = 0.0;
        std::vector<DetectionBox> accepted;
        accepted.reserve(detection.boxes.size());

        for (DetectionBox box : detection.boxes)
        {
            const double rawScore = box.score;
            MotionEvidence evidence;

            if (hasHistory &&
                globalMotionRatio <= smoke_config::GLOBAL_MOTION_MAX_RATIO)
            {
                const cv::Rect motionBox =
                    scaleBox(box.box, frame.size(), motionMask.size());
                evidence = analyzeMotion(motionMask, motionBox);
            }

            const double frameArea =
                static_cast<double>(frame.cols) * static_cast<double>(frame.rows);
            const double boxArea =
                static_cast<double>(box.box.width) * static_cast<double>(box.box.height);
            const double boxAreaRatio = frameArea > 0.0 ? boxArea / frameArea : 0.0;
            const bool oversizedBox =
                boxAreaRatio >= smoke_config::LARGE_BOX_AREA_RATIO;

            // 세트장의 회색 벽/바닥을 화면 전체 연기로 분류하는 오검출을 막는다.
            // 전체 움직임 검증은 느린 실제 연기를 놓칠 수 있으므로 대형 박스에만 적용한다.
            if (oversizedBox && (!hasHistory || !evidence.verified))
                continue;

            // 설정이 false이면 움직임은 진단 라벨에만 남고 YOLO 박스를 제거하지 않는다.
            if (smoke_config::REQUIRE_MOTION_VERIFICATION &&
                (!hasHistory || !evidence.verified))
            {
                continue;
            }

            const double motionStrength = evidence.verified
                ? std::clamp(
                    (evidence.localRatio - smoke_config::MOTION_MIN_RATIO) /
                    (smoke_config::MOTION_FULL_RATIO -
                        smoke_config::MOTION_MIN_RATIO),
                    0.0, 1.0)
                : 0.0;
            const double motionBonus =
                motionStrength * smoke_config::MOTION_MAX_BONUS;
            const double adjustedScore =
                std::clamp(rawScore + motionBonus, 0.0, 1.0);
            if (adjustedScore < smoke_config::CONFIDENCE_THRESHOLD) continue;
            bestAdjustedScore = std::max(bestAdjustedScore, adjustedScore);

            char label[128];
            std::snprintf(label, sizeof(label),
                "smoke %.2f raw %.2f move %.1f%% grid %d/%d",
                adjustedScore,
                rawScore,
                evidence.localRatio * 100.0,
                evidence.activeCells,
                smoke_config::MOTION_GRID_COLUMNS * smoke_config::MOTION_GRID_ROWS);

            box.label = label;
            box.score = adjustedScore;
            accepted.push_back(std::move(box));
        }

        detection.boxes = std::move(accepted);
        detection.maxScore = bestAdjustedScore;
        detection.candidate = !detection.boxes.empty();
    }

    // 모델은 공유하지만 프레임 이력과 시간 누적 상태는 채널마다 분리한다.
    struct SmokeTrack
    {
        int id = -1;
        cv::Rect box;
        double score = 0.0;
        int hits = 0;
        int misses = 0;
        bool confirmed = false;
        SmokeDetectionRuntime::TimePoint lastMatchedTime{};
    };

    struct ChannelState
    {
        cv::Mat pendingFrame;
        std::uint64_t pendingFrameId = 0;
        std::uint64_t pendingEpoch = 0;
        SmokeDetectionRuntime::TimePoint pendingSourceTime{};
        SmokeDetectionRuntime::TimePoint nextAcceptedTime{};
        bool pending = false;

        cv::Mat previousMotionGray;
        SmokeDetectionResult latestDetection;
        std::uint64_t latestResultFrameId = 0;
        std::uint64_t latestResultEpoch = 0;
        SmokeDetectionRuntime::TimePoint latestSourceTime{};
        SmokeDetectionRuntime::TimePoint latestCompletedTime{};
        bool hasResult = false;

        bool smokeDetected = false;
        int positiveHits = 0;
        int consecutiveMisses = 0;
        double latestDetectMs = 0.0;
        double averageDetectMs = 0.0;
        std::vector<SmokeTrack> smokeTracks;
        int nextSmokeTrackId = 1;
        IgnoreRegionFilter ignoreRegionFilter;
        std::uint64_t epoch = 0;
    };

    struct SmokeAssociation
    {
        std::size_t trackIndex = 0;
        std::size_t detectionIndex = 0;
        double score = 0.0;
    };

    void updateSmokeTracks(
        ChannelState& channel,
        SmokeDetectionResult& detection,
        const cv::Size& frameSize,
        SmokeDetectionRuntime::TimePoint sourceTime)
    {
        mergeNearbySmokeBoxes(detection.boxes);

        std::vector<SmokeAssociation> associations;
        for (std::size_t trackIndex = 0;
            trackIndex < channel.smokeTracks.size(); ++trackIndex)
        {
            const SmokeTrack& track = channel.smokeTracks[trackIndex];
            for (std::size_t detectionIndex = 0;
                detectionIndex < detection.boxes.size(); ++detectionIndex)
            {
                const DetectionBox& candidate = detection.boxes[detectionIndex];
                if (!belongsToTrackedRegion(track.box, candidate.box)) continue;

                const double iou = intersectionOverUnion(track.box, candidate.box);
                const double trackCenterX = track.box.x + track.box.width * 0.5;
                const double trackCenterY = track.box.y + track.box.height * 0.5;
                const double candidateCenterX = candidate.box.x + candidate.box.width * 0.5;
                const double candidateCenterY = candidate.box.y + candidate.box.height * 0.5;
                const double distance = std::hypot(
                    candidateCenterX - trackCenterX,
                    candidateCenterY - trackCenterY);
                const double referenceSize = static_cast<double>(std::max({
                    track.box.width, track.box.height,
                    candidate.box.width, candidate.box.height, 1 }));
                const double proximity = 1.0 - std::clamp(
                    distance / referenceSize, 0.0, 1.0);
                associations.push_back({
                    trackIndex,
                    detectionIndex,
                    iou * 2.0 + proximity + candidate.score * 0.05
                });
            }
        }

        std::sort(
            associations.begin(), associations.end(),
            [](const SmokeAssociation& left, const SmokeAssociation& right) {
                return left.score > right.score;
            });

        std::vector<bool> trackMatched(channel.smokeTracks.size(), false);
        std::vector<bool> detectionUsed(detection.boxes.size(), false);
        for (const SmokeAssociation& association : associations)
        {
            if (trackMatched[association.trackIndex] ||
                detectionUsed[association.detectionIndex])
                continue;

            SmokeTrack& track = channel.smokeTracks[association.trackIndex];
            const DetectionBox& candidate = detection.boxes[association.detectionIndex];
            track.box = candidate.box;
            track.score = candidate.score;
            track.hits = std::min(track.hits + 1, smoke_config::CONFIRM_HITS);
            track.misses = 0;
            track.lastMatchedTime = sourceTime;
            if (track.hits >= smoke_config::CONFIRM_HITS)
                track.confirmed = true;
            trackMatched[association.trackIndex] = true;
            detectionUsed[association.detectionIndex] = true;
        }

        for (std::size_t index = 0; index < channel.smokeTracks.size(); ++index)
        {
            if (!trackMatched[index]) channel.smokeTracks[index].misses++;
        }

        for (std::size_t index = 0; index < detection.boxes.size(); ++index)
        {
            if (detectionUsed[index] ||
                channel.smokeTracks.size() >= smoke_config::MAX_TRACKS_PER_CHANNEL)
                continue;

            SmokeTrack track;
            track.id = channel.nextSmokeTrackId++;
            track.box = detection.boxes[index].box;
            track.score = detection.boxes[index].score;
            track.hits = 1;
            track.confirmed = track.hits >= smoke_config::CONFIRM_HITS;
            track.lastMatchedTime = sourceTime;
            channel.smokeTracks.push_back(track);
        }

        channel.smokeTracks.erase(
            std::remove_if(
                channel.smokeTracks.begin(), channel.smokeTracks.end(),
                [sourceTime](const SmokeTrack& track) {
                    if (!track.confirmed)
                        return track.misses >= smoke_config::RELEASE_MISSES;

                    return sourceTime - track.lastMatchedTime >=
                        std::chrono::milliseconds(smoke_config::RELEASE_HOLD_MS);
                }),
            channel.smokeTracks.end());

        std::vector<DetectionBox> confirmedBoxes;
        channel.smokeDetected = false;
        channel.positiveHits = 0;
        channel.consecutiveMisses = smoke_config::RELEASE_HOLD_RESULTS;
        for (const SmokeTrack& track : channel.smokeTracks)
        {
            channel.positiveHits = std::max(channel.positiveHits, track.hits);
            if (!track.confirmed) continue;

            channel.smokeDetected = true;
            channel.consecutiveMisses = std::min(
                channel.consecutiveMisses, track.misses);

            DetectionBox box;
            box.box = track.box;
            box.type = DetectionType::SMOKE;
            box.score = track.score;
            box.trackId = track.id;
            box.trackedPersistenceEvidence = track.misses > 0;
            setNormalizedBoxGeometry(box, frameSize);

            char label[64];
            std::snprintf(label, sizeof(label), "SMOKE %.2f", track.score);
            box.label = label;
            confirmedBoxes.push_back(std::move(box));
        }

        if (!channel.smokeDetected)
            channel.consecutiveMisses = channel.smokeTracks.empty() ?
                smoke_config::RELEASE_HOLD_RESULTS : 0;

        detection.boxes = std::move(confirmedBoxes);
        detection.candidate = !detection.boxes.empty();
    }
}

class SmokeDetectionRuntime::Impl
{
public:
    Impl(std::size_t channelCount, const std::string& paramPath, const std::string& binPath)
        : channels_(std::max<std::size_t>(1, std::min<std::size_t>(
              channelCount, static_cast<std::size_t>(smoke_config::MAX_CHANNELS))))
    {
        const TimePoint now = Clock::now();
        // 채널별 첫 제출 시점을 분산해 동시에 대기열에 들어오는 것을 줄인다.
        for (std::size_t index = 0; index < channels_.size(); ++index)
        {
            const int phaseMs = static_cast<int>(index) *
                smoke_config::SHARED_WORKER_INTERVAL_MS;
            channels_[index].nextAcceptedTime = now + std::chrono::milliseconds(phaseMs);
        }

        modelReady_ = detector_.load(paramPath, binPath);
        modelError_ = detector_.lastError();
        workerThread_ = std::thread(&Impl::workerLoop, this);
    }

    ~Impl() { stop(); }

    bool submitFrame(
        std::size_t channelIndex,
        const cv::Mat& frame,
        std::uint64_t frameId,
        TimePoint sourceTime)
    {
        if (frame.empty() || channelIndex >= channels_.size() || !running_.load() || !modelReady_)
            return false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            ChannelState& channel = channels_[channelIndex];
            if (sourceTime < channel.nextAcceptedTime) return false;

            channel.nextAcceptedTime =
                sourceTime + std::chrono::milliseconds(
                    smoke_config::SHARED_WORKER_INTERVAL_MS *
                    static_cast<int>(channels_.size()));
            // 대기 중이어도 큐를 늘리지 않고 이 채널의 가장 최신 프레임으로 교체한다.
            frame.copyTo(channel.pendingFrame);
            channel.pendingFrameId = frameId;
            channel.pendingEpoch = channel.epoch;
            channel.pendingSourceTime = sourceTime;
            channel.pending = true;
        }
        condition_.notify_one();
        return true;
    }

    bool setIgnoreRegionConfig(
        std::size_t channelIndex,
        const IgnoreRegionConfig& config)
    {
        if (channelIndex >= channels_.size()) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        ChannelState& channel = channels_[channelIndex];
        channel.ignoreRegionFilter.setConfig(config);

        // 설정 전 검출이 새 ROI를 우회해 확정되지 않도록 시간 누적만 초기화한다.
        channel.latestDetection = SmokeDetectionResult{};
        channel.hasResult = false;
        channel.smokeDetected = false;
        channel.positiveHits = 0;
        channel.consecutiveMisses = 0;
        channel.smokeTracks.clear();
        channel.nextSmokeTrackId = 1;
        return true;
    }

    void resetChannel(std::size_t channelIndex)
    {
        if (channelIndex >= channels_.size()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        ChannelState& channel = channels_[channelIndex];
        ++channel.epoch;
        channel.pendingFrame.release();
        channel.pending = false;
        channel.previousMotionGray.release();
        channel.latestDetection = SmokeDetectionResult{};
        channel.latestResultFrameId = 0;
        channel.latestResultEpoch = channel.epoch;
        channel.hasResult = false;
        channel.smokeDetected = false;
        channel.positiveHits = 0;
        channel.consecutiveMisses = 0;
        channel.latestDetectMs = 0.0;
        channel.averageDetectMs = 0.0;
        channel.smokeTracks.clear();
        channel.nextSmokeTrackId = 1;
        const int phaseMs = static_cast<int>(channelIndex) *
            smoke_config::SHARED_WORKER_INTERVAL_MS;
        channel.nextAcceptedTime = Clock::now() + std::chrono::milliseconds(phaseMs);
    }

    SmokeRuntimeSnapshot poll(std::size_t channelIndex, TimePoint now) const
    {
        SmokeRuntimeSnapshot snapshot;
        snapshot.modelReady = modelReady_;
        snapshot.modelError = modelError_;
        if (channelIndex >= channels_.size())
        {
            snapshot.modelError = "Smoke channel index is out of range.";
            return snapshot;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const ChannelState& channel = channels_[channelIndex];
        snapshot.detection = channel.latestDetection;
        snapshot.hasResult = channel.hasResult && channel.latestResultEpoch == channel.epoch;
        snapshot.resultFrameId = channel.latestResultFrameId;
        snapshot.smokeScore = channel.latestDetection.maxScore;
        snapshot.detectMs = channel.latestDetectMs;
        snapshot.averageDetectMs = channel.averageDetectMs;
        snapshot.positiveHits = channel.positiveHits;
        snapshot.consecutiveMisses = channel.consecutiveMisses;

        if (snapshot.hasResult)
        {
            snapshot.resultAgeMs =
                std::chrono::duration<double, std::milli>(now - channel.latestSourceTime).count();
            snapshot.completedAgeMs =
                std::chrono::duration<double, std::milli>(now - channel.latestCompletedTime).count();
            snapshot.pipelineLatencyMs =
                std::chrono::duration<double, std::milli>(
                    channel.latestCompletedTime - channel.latestSourceTime).count();
        }

        // completedAgeMs만 사용하면 큐에서 오래 기다린 프레임도 방금 생성된
        // 결과처럼 보일 수 있다. 완료 당시 파이프라인 지연을 먼저 제한하고,
        // 정상 결과의 화면 유지 시간만 완료 시각을 기준으로 계산한다.
        const bool pipelineLatencyAcceptable = snapshot.hasResult &&
            snapshot.pipelineLatencyMs <= smoke_config::MAX_PIPELINE_LATENCY_MS;
        snapshot.resultIsFresh = snapshot.hasResult &&
            pipelineLatencyAcceptable &&
            snapshot.completedAgeMs <= smoke_config::RESULT_FRESH_MS;
        snapshot.smokeDetected = snapshot.resultIsFresh && channel.smokeDetected;
        snapshot.boxIsFresh = snapshot.resultIsFresh &&
            snapshot.completedAgeMs <= smoke_config::BOX_FRESH_MS &&
            channel.latestDetection.candidate && !channel.latestDetection.boxes.empty();
        return snapshot;
    }

    bool isModelReady() const { return modelReady_; }
    std::string modelError() const { return modelError_; }

    void stop()
    {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false)) return;
        condition_.notify_one();
        if (workerThread_.joinable()) workerThread_.join();
    }

private:
    bool hasPendingFrame() const
    {
        for (const ChannelState& channel : channels_)
        {
            if (channel.pending) return true;
        }
        return false;
    }

    bool takeNextJob(
        std::size_t& channelIndex,
        cv::Mat& frame,
        std::uint64_t& frameId,
        std::uint64_t& epoch,
        TimePoint& sourceTime)
    {
        // 특정 채널이 작업을 독점하지 않도록 마지막 처리 채널 다음부터 탐색한다.
        for (std::size_t offset = 0; offset < channels_.size(); ++offset)
        {
            const std::size_t index = (roundRobinCursor_ + offset) % channels_.size();
            ChannelState& channel = channels_[index];
            if (!channel.pending) continue;

            channelIndex = index;
            frame = channel.pendingFrame;
            frameId = channel.pendingFrameId;
            epoch = channel.pendingEpoch;
            sourceTime = channel.pendingSourceTime;
            channel.pendingFrame.release();
            channel.pending = false;
            roundRobinCursor_ = (index + 1) % channels_.size();
            return true;
        }
        return false;
    }

    void workerLoop()
    {
        while (true)
        {
            std::size_t channelIndex = 0;
            cv::Mat frame;
            std::uint64_t frameId = 0;
            std::uint64_t epoch = 0;
            TimePoint sourceTime{};

            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [&] { return !running_.load() || hasPendingFrame(); });
                if (!running_.load() && !hasPendingFrame()) break;
                if (!takeNextJob(channelIndex, frame, frameId, epoch, sourceTime)) continue;
            }

            const TimePoint started = Clock::now();
            SmokeDetectionResult detection = detector_.detect(frame);
            cv::Mat currentMotionGray;

            std::lock_guard<std::mutex> lock(mutex_);
            ChannelState& channel = channels_[channelIndex];
            if (epoch != channel.epoch) continue;

            applyMotionScore(
                frame, channel.previousMotionGray, detection, currentMotionGray);
            channel.previousMotionGray = std::move(currentMotionGray);

            // 원본 영상이나 NCNN 입력은 가리지 않는다. 검출 및 모션 검증을
            // 마친 박스만 사용자 지정 영역과 비교한 뒤 시간 누적으로 보낸다.
            channel.ignoreRegionFilter.filter(detection.boxes, frame.size());
            detection.candidate = !detection.boxes.empty();
            detection.maxScore = 0.0;
            for (const DetectionBox& box : detection.boxes)
                detection.maxScore = std::max(detection.maxScore, box.score);

            updateSmokeTracks(channel, detection, frame.size(), sourceTime);

            const double detectMs =
                std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            channel.latestDetection = std::move(detection);
            channel.latestResultFrameId = frameId;
            channel.latestResultEpoch = epoch;
            channel.latestSourceTime = sourceTime;
            channel.latestCompletedTime = Clock::now();
            channel.latestDetectMs = detectMs;
            channel.averageDetectMs = channel.averageDetectMs <= 0.0
                ? detectMs
                : channel.averageDetectMs * 0.90 + detectMs * 0.10;
            channel.hasResult = true;

        }
    }

    SmokeDetector detector_;
    bool modelReady_ = false;
    std::string modelError_;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<ChannelState> channels_;
    std::size_t roundRobinCursor_ = 0;
    std::atomic<bool> running_{ true };
    std::thread workerThread_;
};

SmokeDetectionRuntime::SmokeDetectionRuntime(
    std::size_t channelCount,
    const std::string& paramPath,
    const std::string& binPath)
    : impl_(new Impl(channelCount, paramPath, binPath))
{
}

SmokeDetectionRuntime::~SmokeDetectionRuntime() = default;

bool SmokeDetectionRuntime::submitFrame(
    std::size_t channelIndex,
    const cv::Mat& frame,
    std::uint64_t frameId,
    TimePoint sourceTime)
{
    return impl_->submitFrame(channelIndex, frame, frameId, sourceTime);
}

bool SmokeDetectionRuntime::setIgnoreRegionConfig(
    std::size_t channelIndex,
    const IgnoreRegionConfig& config)
{
    return impl_->setIgnoreRegionConfig(channelIndex, config);
}

void SmokeDetectionRuntime::resetChannel(std::size_t channelIndex)
{
    impl_->resetChannel(channelIndex);
}

SmokeRuntimeSnapshot SmokeDetectionRuntime::poll(std::size_t channelIndex, TimePoint now)
{
    return impl_->poll(channelIndex, now);
}

bool SmokeDetectionRuntime::isModelReady() const { return impl_->isModelReady(); }
std::string SmokeDetectionRuntime::modelError() const { return impl_->modelError(); }
void SmokeDetectionRuntime::stop() { impl_->stop(); }
