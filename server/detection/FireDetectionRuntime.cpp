// FireDetectionRuntime.h 구현. 워커 스레드 관리와 결과 스냅샷 갱신을 담당한다.

#include "FireDetectionRuntime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <cmath>
#include <mutex>
#include <numeric>
#include <thread>
#include <unordered_map>
#include <utility>

#include "AppConfig.h"
#include "DisplayRadiusSmoother.h"
#include "FlameDetector.h"
#include "GroundFootprintEstimator.h"
#include "GridCoordinateMapper.h"

using std::lock_guard;
using std::mutex;
using std::unique_lock;

namespace
{
    constexpr std::size_t FIRE_POSITION_HISTORY_LIMIT = 32;
    constexpr std::int64_t FIRE_POSITION_HISTORY_RETENTION_MS = 5000;
    constexpr std::size_t FIRE_REPRESENTATIVE_SMOOTHING_SAMPLES = 5;
    constexpr std::int64_t FIRE_MOTION_WINDOW_MS = 3000;
    constexpr std::int64_t FIRE_MOTION_MIN_WINDOW_MS = 1500;
    constexpr std::size_t FIRE_MOTION_MIN_SAMPLES = 8;
    constexpr double FIRE_MOTION_MIN_DISTANCE = 0.015;
    constexpr double FIRE_MOTION_MAX_ADAPTIVE_DISTANCE = 0.040;
    constexpr double FIRE_MOTION_BOX_DISTANCE_RATIO = 0.08;
    constexpr double FIRE_MOTION_MIN_INLIER_RATIO = 0.75;
    constexpr double FIRE_MOTION_MIN_RESIDUAL_THRESHOLD = 0.006;
    constexpr int FIRE_MOTION_CONFIRM_RESULTS = 2;
    constexpr double FIRE_MOTION_MIN_DIRECTION_DOT = 0.85;
    constexpr double FIRE_GROUP_MERGE_MARGIN_RATIO = 0.35;
    constexpr double FIRE_GROUP_MAX_BOX_DIAGONAL_RATIO = 1.50;
    constexpr double FIRE_GROUP_MAX_FRAME_DIAGONAL_RATIO = 0.12;
    constexpr double FIRE_MOTION_MAX_AREA_CHANGE_RATIO = 0.30;

    enum class InternalAreaTrend { UNKNOWN, SHRINKING, STABLE, GROWING };

    struct FireGroupObservation
    {
        DetectionBox box;
        std::vector<int> memberTrackIds;
    };

    struct FireGroupState
    {
        int id = -1;
        cv::Rect box;
        std::vector<int> memberTrackIds;
        std::int64_t lastSeenTimestampMs = 0;

        std::uint64_t firstSeenFrameId = 0;
        std::int64_t firstSeenTimestampMs = 0;
        double firstSeenNormalizedX = 0.0;
        double firstSeenNormalizedY = 0.0;
        double firstFootprintLeftNormalizedX = 0.0;
        double firstFootprintRightNormalizedX = 0.0;
        double firstFootprintNormalizedY = 0.0;
        double firstArea = 1.0;

        std::size_t lastMemberCount = 0;
        double lastArea = 0.0;
        std::deque<TrackPositionSample> rawAnchorSamples;
        std::deque<TrackPositionSample> samples;
        std::deque<TrackPositionSample> motionSamples;
        std::deque<double> areaHistory;
        std::deque<cv::Rect> boxHistory;
        DisplayRadiusSmoother displayRadiusSmoother;

        // 사용처가 정해지지 않은 분석값은 공개 DetectionBox로 내보내지 않고
        // 그룹 내부에만 보관한다. 면적 변화율은 위치 샘플 제외 판단에도 사용된다.
        InternalAreaTrend areaTrend = InternalAreaTrend::UNKNOWN;
        double areaChangeRatio = 0.0;
        double areaScaleFromFirst = 1.0;
        double recentAreaScale = 1.0;
        bool expansionValid = false;
        bool expansionDirectionValid = false;
        double expansionLeftNormalized = 0.0;
        double expansionRightNormalized = 0.0;
        double expansionUpNormalized = 0.0;
        double expansionDownNormalized = 0.0;
        double expansionDirectionX = 0.0;
        double expansionDirectionY = 0.0;
        double expansionMagnitudeNormalized = 0.0;
        int motionCandidateHits = 0;
        double candidateDirectionX = 0.0;
        double candidateDirectionY = 0.0;
        bool motionConfirmed = false;
        double confirmedDirectionX = 0.0;
        double confirmedDirectionY = 0.0;
        double confirmedSpeed = 0.0;
        std::int64_t confirmedWindowMs = 0;
    };

    cv::Rect expandedFireBox(const cv::Rect& box)
    {
        const int marginX = std::max(2, static_cast<int>(std::lround(
            box.width * FIRE_GROUP_MERGE_MARGIN_RATIO)));
        const int marginY = std::max(2, static_cast<int>(std::lround(
            box.height * FIRE_GROUP_MERGE_MARGIN_RATIO)));
        return cv::Rect(
            box.x - marginX,
            box.y - marginY,
            box.width + marginX * 2,
            box.height + marginY * 2);
    }

    double fireBoxIou(const cv::Rect& left, const cv::Rect& right)
    {
        if (left.empty() || right.empty()) return 0.0;
        const cv::Rect intersection = left & right;
        const double intersectionArea = intersection.empty() ? 0.0 : intersection.area();
        const double unionArea = static_cast<double>(left.area() + right.area()) -
            intersectionArea;
        return unionArea > 0.0 ? intersectionArea / unionArea : 0.0;
    }

    bool nearbyFireBoxes(
        const cv::Rect& left,
        const cv::Rect& right,
        const cv::Size& frameSize)
    {
        if (left.empty() || right.empty()) return false;
        if (!(expandedFireBox(left) & expandedFireBox(right)).empty()) return true;

        const cv::Point2d leftCenter(
            left.x + left.width * 0.5, left.y + left.height * 0.5);
        const cv::Point2d rightCenter(
            right.x + right.width * 0.5, right.y + right.height * 0.5);
        const double boxReference = std::max(
            std::hypot(static_cast<double>(left.width), static_cast<double>(left.height)),
            std::hypot(static_cast<double>(right.width), static_cast<double>(right.height)));
        const double frameReference = std::hypot(
            static_cast<double>(std::max(1, frameSize.width)),
            static_cast<double>(std::max(1, frameSize.height)));
        const double maximumDistance = std::max(
            boxReference * FIRE_GROUP_MAX_BOX_DIAGONAL_RATIO,
            frameReference * FIRE_GROUP_MAX_FRAME_DIAGONAL_RATIO);
        return cv::norm(leftCenter - rightCenter) <= maximumDistance;
    }

    int sharedTrackCount(
        const std::vector<int>& left,
        const std::vector<int>& right)
    {
        int count = 0;
        for (const int leftId : left)
        {
            if (std::find(right.begin(), right.end(), leftId) != right.end())
                ++count;
        }
        return count;
    }

    double medianValue(std::vector<double> values)
    {
        if (values.empty()) return 0.0;
        const std::size_t middle = values.size() / 2;
        std::nth_element(values.begin(), values.begin() + middle, values.end());
        const double upper = values[middle];
        if ((values.size() & 1U) != 0U) return upper;
        std::nth_element(values.begin(), values.begin() + middle - 1, values.end());
        return (values[middle - 1] + upper) * 0.5;
    }

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

    bool loadArucoBoardConfiguration(
        const std::string& configurationPath,
        std::size_t channelIndex)
    {
        return gridMapper_.loadArucoBoardConfiguration(
            configurationPath, channelIndex);
    }

    std::string arucoMappingError() const
    {
        return gridMapper_.lastError();
    }

    bool loadCameraCalibration(
        const std::string& calibrationPath,
        std::size_t channelIndex)
    {
        return gridMapper_.loadCameraCalibration(calibrationPath, channelIndex);
    }

    bool loadStaticHomography(
        const std::string& homographyPath,
        std::size_t channelIndex)
    {
        return gridMapper_.loadStaticHomography(homographyPath, channelIndex);
    }

    std::string cameraCalibrationError() const
    {
        return gridMapper_.cameraCalibrationError();
    }

    void resetStream()
    {
        // epoch가 다른 진행 중 결과는 작업 완료 후에도 게시되지 않는다.
        streamEpoch_.fetch_add(1);
        detectorResetRequested_ = true;
        gridMapper_.resetTracking();

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
        snapshot.arucoMapping = gridMapper_.status();

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
            fireGroups_.clear();
            nextFireGroupId_ = 1;
            appliedIgnoreConfigVersion_ = pendingIgnoreConfigVersion_;
        }
        return appliedIgnoreConfigVersion_;
    }

    std::vector<FireGroupObservation> buildFireGroupObservations(
        const std::vector<DetectionBox>& boxes,
        const cv::Size& frameSize) const
    {
        std::vector<FireGroupObservation> observations;
        if (boxes.empty()) return observations;

        std::vector<int> parent(boxes.size());
        std::iota(parent.begin(), parent.end(), 0);
        auto findRoot = [&](int index)
        {
            while (parent[index] != index)
            {
                parent[index] = parent[parent[index]];
                index = parent[index];
            }
            return index;
        };

        for (std::size_t left = 0; left < boxes.size(); ++left)
        {
            if (boxes[left].type != DetectionType::FIRE || boxes[left].box.empty()) continue;
            for (std::size_t right = left + 1; right < boxes.size(); ++right)
            {
                if (boxes[right].type != DetectionType::FIRE || boxes[right].box.empty() ||
                    !nearbyFireBoxes(boxes[left].box, boxes[right].box, frameSize))
                    continue;

                const int leftRoot = findRoot(static_cast<int>(left));
                const int rightRoot = findRoot(static_cast<int>(right));
                if (leftRoot != rightRoot) parent[rightRoot] = leftRoot;
            }
        }

        std::unordered_map<int, std::vector<std::size_t>> clusters;
        for (std::size_t index = 0; index < boxes.size(); ++index)
        {
            if (boxes[index].type == DetectionType::FIRE && !boxes[index].box.empty())
                clusters[findRoot(static_cast<int>(index))].push_back(index);
        }

        observations.reserve(clusters.size());
        for (const auto& entry : clusters)
        {
            const std::vector<std::size_t>& members = entry.second;
            const auto best = std::max_element(
                members.begin(), members.end(),
                [&](std::size_t left, std::size_t right)
                {
                    return boxes[left].score < boxes[right].score;
                });

            FireGroupObservation observation;
            observation.box = boxes[*best];
            cv::Rect unionBox;
            bool allHeld = true;
            for (const std::size_t memberIndex : members)
            {
                const DetectionBox& member = boxes[memberIndex];
                unionBox = unionBox.empty() ? member.box : (unionBox | member.box);
                if (member.trackId >= 0)
                    observation.memberTrackIds.push_back(member.trackId);
                allHeld = allHeld && member.trackedPersistenceEvidence;
            }
            std::sort(observation.memberTrackIds.begin(), observation.memberTrackIds.end());
            observation.memberTrackIds.erase(
                std::unique(
                    observation.memberTrackIds.begin(), observation.memberTrackIds.end()),
                observation.memberTrackIds.end());
            observation.box.box = unionBox;
            observation.box.groupedTrackCount = static_cast<int>(members.size());
            observation.box.trackedPersistenceEvidence = allHeld;
            observations.push_back(std::move(observation));
        }
        return observations;
    }

    void setGroupedFireGeometry(DetectionBox& box, const cv::Size& frameSize) const
    {
        if (frameSize.width <= 0 || frameSize.height <= 0 || box.box.empty()) return;
        box.normalizedX = std::clamp(
            static_cast<double>(box.box.x) / std::max(1, frameSize.width), 0.0, 1.0);
        box.normalizedY = std::clamp(
            static_cast<double>(box.box.y) / std::max(1, frameSize.height), 0.0, 1.0);
        box.normalizedWidth = std::clamp(
            static_cast<double>(box.box.width) / std::max(1, frameSize.width), 0.0, 1.0);
        box.normalizedHeight = std::clamp(
            static_cast<double>(box.box.height) / std::max(1, frameSize.height), 0.0, 1.0);

        const double maxX = static_cast<double>(std::max(1, frameSize.width - 1));
        const double maxY = static_cast<double>(std::max(1, frameSize.height - 1));
        const double left = std::clamp(static_cast<double>(box.box.x) / maxX, 0.0, 1.0);
        const double right = std::clamp(
            static_cast<double>(box.box.x + std::max(0, box.box.width - 1)) / maxX,
            0.0, 1.0);
        const double bottom = std::clamp(
            static_cast<double>(box.box.y + std::max(0, box.box.height - 1)) / maxY,
            0.0, 1.0);
        box.representativePositionValid = true;
        box.representativeNormalizedX = (left + right) * 0.5;
        box.representativeNormalizedY = bottom;
        box.fireFootprintValid = true;
        box.footprintLeftNormalizedX = left;
        box.footprintRightNormalizedX = right;
        box.footprintNormalizedY = bottom;
    }

    void attachGridPosition(DetectionBox& box, FireGroupState& group)
    {
        box.gridPositionValid = false;
        box.gridX = -1;
        box.gridY = -1;
        box.factoryPositionValid = false;
        box.factoryXMetres = 0.0;
        box.factoryYMetres = 0.0;
        box.factoryFootprintValid = false;
        box.footprintLeftFactoryXMetres = 0.0;
        box.footprintLeftFactoryYMetres = 0.0;
        box.footprintRightFactoryXMetres = 0.0;
        box.footprintRightFactoryYMetres = 0.0;
        box.estimatedGroundWidthMetres = 0.0;
        box.estimatedGroundAreaSquareMetres = 0.0;
        box.footprintLeftGridX = -1;
        box.footprintLeftGridY = -1;
        box.footprintRightGridX = -1;
        box.footprintRightGridY = -1;
        box.displayRadiusCells = 0;

        cv::Point2f normalizedPosition;
        if (box.smoothedRepresentativePositionValid)
        {
            normalizedPosition.x =
                static_cast<float>(box.smoothedRepresentativeNormalizedX);
            normalizedPosition.y =
                static_cast<float>(box.smoothedRepresentativeNormalizedY);
        }
        else if (box.representativePositionValid)
        {
            normalizedPosition.x =
                static_cast<float>(box.representativeNormalizedX);
            normalizedPosition.y =
                static_cast<float>(box.representativeNormalizedY);
        }
        else
        {
            return;
        }

        cv::Point gridPoint;
        cv::Point2f factoryPointM;
        if (!gridMapper_.map(
            normalizedPosition, gridPoint, &factoryPointM)) return;
        box.gridPositionValid = true;
        box.gridX = gridPoint.x;
        box.gridY = gridPoint.y;
        box.factoryPositionValid = true;
        box.factoryXMetres = factoryPointM.x;
        box.factoryYMetres = factoryPointM.y;

        if (!box.fireFootprintValid) return;

        const cv::Point2f normalizedLeft(
            static_cast<float>(box.footprintLeftNormalizedX),
            static_cast<float>(box.footprintNormalizedY));
        const cv::Point2f normalizedRight(
            static_cast<float>(box.footprintRightNormalizedX),
            static_cast<float>(box.footprintNormalizedY));
        cv::Point leftGrid;
        cv::Point rightGrid;
        cv::Point2f leftFactoryM;
        cv::Point2f rightFactoryM;
        if (!gridMapper_.map(normalizedLeft, leftGrid, &leftFactoryM) ||
            !gridMapper_.map(normalizedRight, rightGrid, &rightFactoryM))
            return;

        const GroundFootprintEstimate footprint = estimateCircularGroundFootprint(
            leftFactoryM, rightFactoryM, leftGrid, rightGrid);
        if (!footprint.valid) return;

        box.factoryFootprintValid = true;
        box.footprintLeftFactoryXMetres = leftFactoryM.x;
        box.footprintLeftFactoryYMetres = leftFactoryM.y;
        box.footprintRightFactoryXMetres = rightFactoryM.x;
        box.footprintRightFactoryYMetres = rightFactoryM.y;
        box.estimatedGroundWidthMetres = footprint.widthMetres;
        box.estimatedGroundAreaSquareMetres = footprint.areaSquareMetres;
        box.footprintLeftGridX = leftGrid.x;
        box.footprintLeftGridY = leftGrid.y;
        box.footprintRightGridX = rightGrid.x;
        box.footprintRightGridY = rightGrid.y;

        box.displayRadiusCells =
            group.displayRadiusSmoother.update(footprint.displayRadiusCells);
    }

    void clearMotionEvidence(FireGroupState& group) const
    {
        group.motionCandidateHits = 0;
        group.candidateDirectionX = 0.0;
        group.candidateDirectionY = 0.0;
        group.motionConfirmed = false;
    }

    void attachGroupAnalysis(
        FireGroupState& group,
        DetectionBox& box,
        const std::vector<int>& currentMemberTrackIds,
        std::uint64_t frameId,
        std::int64_t timestampMs,
        const cv::Size& frameSize)
    {
        const bool firstObservation = group.lastMemberCount == 0;
        const int retainedMembers = sharedTrackCount(
            group.memberTrackIds, currentMemberTrackIds);
        const bool membershipChanged = !firstObservation &&
            (group.lastMemberCount != currentMemberTrackIds.size() ||
                (!group.memberTrackIds.empty() && retainedMembers == 0));
        const double currentArea = static_cast<double>(std::max(1, box.box.area()));
        const double areaChangeRatio = group.lastArea > 0.0 ?
            std::abs(currentArea - group.lastArea) / group.lastArea : 0.0;
        const bool largeSizeChange = !firstObservation &&
            areaChangeRatio > FIRE_MOTION_MAX_AREA_CHANGE_RATIO;
        const bool stableForMotion = !box.trackedPersistenceEvidence &&
            !membershipChanged && !largeSizeChange;

        if (firstObservation)
        {
            group.firstSeenFrameId = frameId;
            group.firstSeenTimestampMs = timestampMs;
            group.firstSeenNormalizedX = box.representativeNormalizedX;
            group.firstSeenNormalizedY = box.representativeNormalizedY;
            group.firstFootprintLeftNormalizedX = box.footprintLeftNormalizedX;
            group.firstFootprintRightNormalizedX = box.footprintRightNormalizedX;
            group.firstFootprintNormalizedY = box.footprintNormalizedY;
            group.firstArea = currentArea;
        }

        group.areaScaleFromFirst = currentArea / std::max(1.0, group.firstArea);

        bool positionSampleAdded = false;
        if ((firstObservation || stableForMotion) &&
            (group.samples.empty() || group.samples.back().frameId != frameId))
        {
            TrackPositionSample rawSample;
            rawSample.frameId = frameId;
            rawSample.timestampMs = timestampMs;
            rawSample.normalizedX = box.representativeNormalizedX;
            rawSample.normalizedY = box.representativeNormalizedY;
            group.rawAnchorSamples.push_back(rawSample);
            while (group.rawAnchorSamples.size() >
                FIRE_REPRESENTATIVE_SMOOTHING_SAMPLES)
                group.rawAnchorSamples.pop_front();

            std::vector<double> anchorX;
            std::vector<double> anchorY;
            anchorX.reserve(group.rawAnchorSamples.size());
            anchorY.reserve(group.rawAnchorSamples.size());
            for (const TrackPositionSample& anchor : group.rawAnchorSamples)
            {
                anchorX.push_back(anchor.normalizedX);
                anchorY.push_back(anchor.normalizedY);
            }

            TrackPositionSample sample = rawSample;
            sample.normalizedX = medianValue(std::move(anchorX));
            sample.normalizedY = medianValue(std::move(anchorY));
            group.samples.push_back(sample);
            group.motionSamples.push_back(sample);
            positionSampleAdded = true;
            while (group.samples.size() > FIRE_POSITION_HISTORY_LIMIT)
                group.samples.pop_front();
            while (group.motionSamples.size() > FIRE_POSITION_HISTORY_LIMIT)
                group.motionSamples.pop_front();
        }
        else if (membershipChanged)
        {
            // A merge/split changes the physical meaning of the group anchor.
            group.rawAnchorSamples.clear();
            group.samples.clear();
            group.motionSamples.clear();
            clearMotionEvidence(group);
        }

        box.positionHistory.assign(group.samples.begin(), group.samples.end());
        if (!group.samples.empty())
        {
            const TrackPositionSample& stableAnchor = group.samples.back();
            box.smoothedRepresentativePositionValid = true;
            box.smoothedRepresentativeNormalizedX = stableAnchor.normalizedX;
            box.smoothedRepresentativeNormalizedY = stableAnchor.normalizedY;
        }

        std::vector<const TrackPositionSample*> motionSamples;
        if (!group.motionSamples.empty())
        {
            const TrackPositionSample& latest = group.motionSamples.back();
            for (const TrackPositionSample& sample : group.motionSamples)
            {
                if (latest.timestampMs - sample.timestampMs <= FIRE_MOTION_WINDOW_MS)
                    motionSamples.push_back(&sample);
            }
        }

        if (positionSampleAdded && motionSamples.size() >= FIRE_MOTION_MIN_SAMPLES)
        {
            const std::int64_t elapsedMs =
                motionSamples.back()->timestampMs - motionSamples.front()->timestampMs;
            std::vector<double> slopeX;
            std::vector<double> slopeY;
            for (std::size_t left = 0; left < motionSamples.size(); ++left)
            {
                for (std::size_t right = left + 1; right < motionSamples.size(); ++right)
                {
                    const double deltaSeconds = static_cast<double>(
                        motionSamples[right]->timestampMs - motionSamples[left]->timestampMs) /
                        1000.0;
                    if (deltaSeconds < 0.25) continue;
                    slopeX.push_back(
                        (motionSamples[right]->normalizedX -
                            motionSamples[left]->normalizedX) / deltaSeconds);
                    slopeY.push_back(
                        (motionSamples[right]->normalizedY -
                            motionSamples[left]->normalizedY) / deltaSeconds);
                }
            }

            bool validMotion = elapsedMs >= FIRE_MOTION_MIN_WINDOW_MS &&
                !slopeX.empty();
            double velocityX = validMotion ? medianValue(std::move(slopeX)) : 0.0;
            double velocityY = validMotion ? medianValue(std::move(slopeY)) : 0.0;
            const std::int64_t originTimestampMs = motionSamples.front()->timestampMs;
            std::vector<double> interceptX;
            std::vector<double> interceptY;
            std::vector<double> residuals;
            if (validMotion)
            {
                interceptX.reserve(motionSamples.size());
                interceptY.reserve(motionSamples.size());
                for (const TrackPositionSample* sample : motionSamples)
                {
                    const double timeSeconds = static_cast<double>(
                        sample->timestampMs - originTimestampMs) / 1000.0;
                    interceptX.push_back(sample->normalizedX - velocityX * timeSeconds);
                    interceptY.push_back(sample->normalizedY - velocityY * timeSeconds);
                }
                const double originX = medianValue(std::move(interceptX));
                const double originY = medianValue(std::move(interceptY));
                residuals.reserve(motionSamples.size());
                for (const TrackPositionSample* sample : motionSamples)
                {
                    const double timeSeconds = static_cast<double>(
                        sample->timestampMs - originTimestampMs) / 1000.0;
                    residuals.push_back(std::hypot(
                        sample->normalizedX - (originX + velocityX * timeSeconds),
                        sample->normalizedY - (originY + velocityY * timeSeconds)));
                }
                const double medianResidual = medianValue(residuals);
                std::vector<double> residualDeviation;
                residualDeviation.reserve(residuals.size());
                for (const double residual : residuals)
                    residualDeviation.push_back(std::abs(residual - medianResidual));
                const double residualMad = medianValue(std::move(residualDeviation));
                const double residualThreshold = std::max(
                    FIRE_MOTION_MIN_RESIDUAL_THRESHOLD,
                    medianResidual + 3.0 * std::max(0.001, residualMad));

                std::vector<std::size_t> inliers;
                for (std::size_t index = 0; index < residuals.size(); ++index)
                {
                    if (residuals[index] <= residualThreshold)
                        inliers.push_back(index);
                }
                validMotion = inliers.size() >= FIRE_MOTION_MIN_SAMPLES &&
                    static_cast<double>(inliers.size()) / motionSamples.size() >=
                        FIRE_MOTION_MIN_INLIER_RATIO;

                if (validMotion)
                {
                    double meanTime = 0.0;
                    double meanX = 0.0;
                    double meanY = 0.0;
                    for (const std::size_t index : inliers)
                    {
                        meanTime += static_cast<double>(
                            motionSamples[index]->timestampMs - originTimestampMs) / 1000.0;
                        meanX += motionSamples[index]->normalizedX;
                        meanY += motionSamples[index]->normalizedY;
                    }
                    const double inlierCount = static_cast<double>(inliers.size());
                    meanTime /= inlierCount;
                    meanX /= inlierCount;
                    meanY /= inlierCount;
                    double denominator = 0.0;
                    double numeratorX = 0.0;
                    double numeratorY = 0.0;
                    for (const std::size_t index : inliers)
                    {
                        const double timeSeconds = static_cast<double>(
                            motionSamples[index]->timestampMs - originTimestampMs) / 1000.0;
                        const double centeredTime = timeSeconds - meanTime;
                        denominator += centeredTime * centeredTime;
                        numeratorX += centeredTime *
                            (motionSamples[index]->normalizedX - meanX);
                        numeratorY += centeredTime *
                            (motionSamples[index]->normalizedY - meanY);
                    }
                    validMotion = denominator > 1e-9;
                    if (validMotion)
                    {
                        velocityX = numeratorX / denominator;
                        velocityY = numeratorY / denominator;
                    }
                }
            }

            const double speed = std::hypot(velocityX, velocityY);
            const double displacement = speed * static_cast<double>(elapsedMs) / 1000.0;
            const double boxDiagonal = std::hypot(
                box.normalizedWidth, box.normalizedHeight);
            const double requiredDistance = std::clamp(
                boxDiagonal * FIRE_MOTION_BOX_DISTANCE_RATIO,
                FIRE_MOTION_MIN_DISTANCE,
                FIRE_MOTION_MAX_ADAPTIVE_DISTANCE);
            validMotion = validMotion && speed > 0.0 &&
                displacement >= requiredDistance;

            if (validMotion)
            {
                const double directionX = velocityX / speed;
                const double directionY = velocityY / speed;
                const double directionDot =
                    directionX * group.candidateDirectionX +
                    directionY * group.candidateDirectionY;
                if (group.motionCandidateHits > 0 &&
                    directionDot >= FIRE_MOTION_MIN_DIRECTION_DOT)
                    ++group.motionCandidateHits;
                else
                    group.motionCandidateHits = 1;

                group.candidateDirectionX = directionX;
                group.candidateDirectionY = directionY;
                group.motionConfirmed =
                    group.motionCandidateHits >= FIRE_MOTION_CONFIRM_RESULTS;
                if (group.motionConfirmed)
                {
                    group.confirmedDirectionX = directionX;
                    group.confirmedDirectionY = directionY;
                    group.confirmedSpeed = speed;
                    group.confirmedWindowMs = elapsedMs;
                }
            }
            else
            {
                clearMotionEvidence(group);
            }
        }
        else if (positionSampleAdded)
        {
            clearMotionEvidence(group);
        }

        if (!box.trackedPersistenceEvidence)
        {
            if (membershipChanged)
            {
                // Do not count a group merge/split itself as fire expansion.
                group.areaHistory.clear();
                group.boxHistory.clear();
            }
            group.areaHistory.push_back(currentArea);
            group.boxHistory.push_back(box.box);
            if (group.areaHistory.size() > 16) group.areaHistory.pop_front();
            if (group.boxHistory.size() > 16) group.boxHistory.pop_front();
        }

        group.areaTrend = InternalAreaTrend::UNKNOWN;
        group.areaChangeRatio = 0.0;
        group.expansionValid = false;
        group.expansionDirectionValid = false;
        group.expansionLeftNormalized = 0.0;
        group.expansionRightNormalized = 0.0;
        group.expansionUpNormalized = 0.0;
        group.expansionDownNormalized = 0.0;
        group.expansionDirectionX = 0.0;
        group.expansionDirectionY = 0.0;
        group.expansionMagnitudeNormalized = 0.0;
        group.recentAreaScale = 1.0;

        if (group.areaHistory.size() >= 6 && group.boxHistory.size() >= 6)
        {
            constexpr std::size_t sampleCount = 3;
            double referenceArea = 0.0;
            double latestArea = 0.0;
            for (std::size_t index = 0; index < sampleCount; ++index)
            {
                referenceArea += group.areaHistory[index];
                latestArea += group.areaHistory[group.areaHistory.size() - sampleCount + index];
            }
            referenceArea /= sampleCount;
            latestArea /= sampleCount;
            group.areaChangeRatio = referenceArea > 0.0 ?
                (latestArea - referenceArea) / referenceArea : 0.0;
            if (group.areaChangeRatio >= 0.20)
                group.areaTrend = InternalAreaTrend::GROWING;
            else if (group.areaChangeRatio <= -0.20)
                group.areaTrend = InternalAreaTrend::SHRINKING;
            else
                group.areaTrend = InternalAreaTrend::STABLE;
            group.recentAreaScale =
                referenceArea > 0.0 ? latestArea / referenceArea : 1.0;

            const double maxX = static_cast<double>(std::max(1, frameSize.width - 1));
            const double maxY = static_cast<double>(std::max(1, frameSize.height - 1));
            double referenceLeft = 0.0, referenceRight = 0.0;
            double referenceTop = 0.0, referenceBottom = 0.0;
            double latestLeft = 0.0, latestRight = 0.0;
            double latestTop = 0.0, latestBottom = 0.0;
            for (std::size_t index = 0; index < sampleCount; ++index)
            {
                const cv::Rect& reference = group.boxHistory[index];
                const cv::Rect& latest =
                    group.boxHistory[group.boxHistory.size() - sampleCount + index];
                referenceLeft += reference.x / maxX;
                referenceRight +=
                    (reference.x + std::max(0, reference.width - 1)) / maxX;
                referenceTop += reference.y / maxY;
                referenceBottom +=
                    (reference.y + std::max(0, reference.height - 1)) / maxY;
                latestLeft += latest.x / maxX;
                latestRight += (latest.x + std::max(0, latest.width - 1)) / maxX;
                latestTop += latest.y / maxY;
                latestBottom += (latest.y + std::max(0, latest.height - 1)) / maxY;
            }
            const double count = static_cast<double>(sampleCount);
            referenceLeft /= count; referenceRight /= count;
            referenceTop /= count; referenceBottom /= count;
            latestLeft /= count; latestRight /= count;
            latestTop /= count; latestBottom /= count;
            group.expansionLeftNormalized = std::max(0.0, referenceLeft - latestLeft);
            group.expansionRightNormalized = std::max(0.0, latestRight - referenceRight);
            group.expansionUpNormalized = std::max(0.0, referenceTop - latestTop);
            group.expansionDownNormalized = std::max(0.0, latestBottom - referenceBottom);
            const double expansionX =
                group.expansionRightNormalized - group.expansionLeftNormalized;
            const double expansionY =
                group.expansionDownNormalized - group.expansionUpNormalized;
            group.expansionMagnitudeNormalized = std::hypot(expansionX, expansionY);
            group.expansionValid = group.areaTrend == InternalAreaTrend::GROWING &&
                (group.expansionLeftNormalized + group.expansionRightNormalized +
                    group.expansionUpNormalized + group.expansionDownNormalized) >= 0.01;
            if (group.expansionValid && group.expansionMagnitudeNormalized >= 0.005)
            {
                group.expansionDirectionValid = true;
                group.expansionDirectionX = expansionX / group.expansionMagnitudeNormalized;
                group.expansionDirectionY = expansionY / group.expansionMagnitudeNormalized;
            }
        }

        group.lastArea = currentArea;
        group.lastMemberCount = static_cast<std::size_t>(box.groupedTrackCount);
    }

    void updateFireGroups(
        DetectionResult& detection,
        const cv::Size& frameSize,
        std::uint64_t frameId,
        TimePoint sourceTime)
    {
        const std::int64_t timestampMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                sourceTime.time_since_epoch()).count();

        fireGroups_.erase(
            std::remove_if(
                fireGroups_.begin(), fireGroups_.end(),
                [&](const FireGroupState& group)
                {
                    return timestampMs - group.lastSeenTimestampMs >
                        FIRE_POSITION_HISTORY_RETENTION_MS;
                }),
            fireGroups_.end());

        std::vector<FireGroupObservation> observations =
            buildFireGroupObservations(detection.boxes, frameSize);
        struct GroupAssociation
        {
            std::size_t groupIndex = 0;
            std::size_t observationIndex = 0;
            double score = 0.0;
        };
        std::vector<GroupAssociation> associations;
        for (std::size_t groupIndex = 0; groupIndex < fireGroups_.size(); ++groupIndex)
        {
            const FireGroupState& group = fireGroups_[groupIndex];
            for (std::size_t observationIndex = 0;
                observationIndex < observations.size(); ++observationIndex)
            {
                const FireGroupObservation& observation = observations[observationIndex];
                const int shared = sharedTrackCount(
                    group.memberTrackIds, observation.memberTrackIds);
                if (shared == 0 &&
                    !nearbyFireBoxes(group.box, observation.box.box, frameSize))
                    continue;

                const double iou = fireBoxIou(group.box, observation.box.box);
                const cv::Point2d groupCenter(
                    group.box.x + group.box.width * 0.5,
                    group.box.y + group.box.height * 0.5);
                const cv::Point2d observationCenter(
                    observation.box.box.x + observation.box.box.width * 0.5,
                    observation.box.box.y + observation.box.box.height * 0.5);
                const double referenceSize = static_cast<double>(std::max({
                    group.box.width, group.box.height,
                    observation.box.box.width, observation.box.box.height, 1 }));
                const double proximity = 1.0 - std::clamp(
                    cv::norm(groupCenter - observationCenter) / referenceSize, 0.0, 1.0);
                associations.push_back({
                    groupIndex,
                    observationIndex,
                    shared * 100.0 + iou * 2.0 + proximity
                });
            }
        }

        std::sort(
            associations.begin(), associations.end(),
            [](const GroupAssociation& left, const GroupAssociation& right)
            {
                return left.score > right.score;
            });
        std::vector<bool> groupMatched(fireGroups_.size(), false);
        std::vector<int> assignedGroup(observations.size(), -1);
        for (const GroupAssociation& association : associations)
        {
            if (groupMatched[association.groupIndex] ||
                assignedGroup[association.observationIndex] >= 0)
                continue;
            groupMatched[association.groupIndex] = true;
            assignedGroup[association.observationIndex] =
                static_cast<int>(association.groupIndex);
        }

        for (std::size_t observationIndex = 0;
            observationIndex < observations.size(); ++observationIndex)
        {
            if (assignedGroup[observationIndex] >= 0) continue;
            FireGroupState group;
            group.id = nextFireGroupId_++;
            fireGroups_.push_back(std::move(group));
            assignedGroup[observationIndex] = static_cast<int>(fireGroups_.size() - 1);
        }

        std::vector<DetectionBox> groupedBoxes;
        groupedBoxes.reserve(observations.size());
        double totalArea = 0.0;
        for (std::size_t observationIndex = 0;
            observationIndex < observations.size(); ++observationIndex)
        {
            FireGroupObservation& observation = observations[observationIndex];
            FireGroupState& group = fireGroups_[assignedGroup[observationIndex]];
            DetectionBox box = std::move(observation.box);
            box.trackId = group.id;
            setGroupedFireGeometry(box, frameSize);
            attachGroupAnalysis(
                group, box, observation.memberTrackIds,
                frameId, timestampMs, frameSize);
            attachGridPosition(box, group);
            group.box = box.box;
            group.memberTrackIds = std::move(observation.memberTrackIds);
            group.lastSeenTimestampMs = timestampMs;

            char label[96];
            std::snprintf(label, sizeof(label), "FIRE GROUP %d x%d %.2f",
                group.id, box.groupedTrackCount, box.score);
            box.label = label;
            totalArea += box.box.area();
            groupedBoxes.push_back(std::move(box));
        }

        detection.boxes = std::move(groupedBoxes);
        detection.detected = !detection.boxes.empty();
        detection.area = totalArea;
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
                fireGroups_.clear();
                nextFireGroupId_ = 1;
                cameraHealthMonitor_.reset();
            }

            const std::uint64_t ignoreConfigVersion = applyPendingIgnoreConfig();

            // 운영 서버는 설치 보정에서 저장한 고정 Homography만 사용한다.
            // 매 화재 프레임에서 ArUco를 다시 찾지 않아 Raspberry Pi 부하와
            // 마커 가림에 따른 좌표 흔들림을 피한다. ArUco 대응점 검출은
            // FixedHomographyCalibrator가 담당한다.

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

            updateFireGroups(detection, frame.size(), frameId, sourceTime);

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
    GridCoordinateMapper gridMapper_;
    CameraHealthMonitor cameraHealthMonitor_;
    std::vector<FireGroupState> fireGroups_;
    int nextFireGroupId_ = 1;
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

bool FireDetectionRuntime::loadArucoBoardConfiguration(
    const std::string& configurationPath,
    std::size_t channelIndex)
{
    return impl_->loadArucoBoardConfiguration(
        configurationPath, channelIndex);
}

std::string FireDetectionRuntime::arucoMappingError() const
{
    return impl_->arucoMappingError();
}

bool FireDetectionRuntime::loadCameraCalibration(
    const std::string& calibrationPath,
    std::size_t channelIndex)
{
    return impl_->loadCameraCalibration(calibrationPath, channelIndex);
}

bool FireDetectionRuntime::loadStaticHomography(
    const std::string& homographyPath,
    std::size_t channelIndex)
{
    return impl_->loadStaticHomography(homographyPath, channelIndex);
}

std::string FireDetectionRuntime::cameraCalibrationError() const
{
    return impl_->cameraCalibrationError();
}

void FireDetectionRuntime::resetStream() { impl_->resetStream(); }
FireRuntimeSnapshot FireDetectionRuntime::poll(TimePoint now) { return impl_->poll(now); }
void FireDetectionRuntime::stop() { impl_->stop(); }
