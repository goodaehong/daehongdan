// FireAlarmController.h 구현. 연속 검출 누적과 해제 유지 시간을 계산한다.

#include "FireAlarmController.h"

#include <algorithm>
#include <cmath>

using std::max;
using std::min;

constexpr double FireAlarmController::FINAL_CONFIRM_MS;
constexpr int FireAlarmController::MIN_RAW_FIRE_RESULTS;
constexpr double FireAlarmController::AMBIGUOUS_CONFIRM_MS;
constexpr int FireAlarmController::MIN_AMBIGUOUS_RAW_FIRE_RESULTS;
constexpr double FireAlarmController::DEFAULT_RESULT_INTERVAL_MS;
constexpr double FireAlarmController::MIN_RESULT_INTERVAL_MS;
constexpr double FireAlarmController::MAX_RESULT_INTERVAL_MS;
constexpr double FireAlarmController::PRECONFIRM_DECAY_RATE;
constexpr double FireAlarmController::SOFT_CANDIDATE_DECAY_RATE;
constexpr double FireAlarmController::POSTCONFIRM_DECAY_RATE;
constexpr double FireAlarmController::RELEASE_EVIDENCE_RATIO;
constexpr double FireAlarmController::DETECTOR_HOLD_GAIN;

FireAlarmController::FireAlarmController() { reset(); }

void FireAlarmController::reset()
{
    finalFireAlarm_ = false;
    rawFireResultCount_ = 0;
    fireEvidenceMs_ = 0.0;
    activeConfirmMs_ = FINAL_CONFIRM_MS;
    activeMinRawFireResults_ = MIN_RAW_FIRE_RESULTS;
    activeAmbiguousWarmObject_ = false;
    activeCandidateBox_ = cv::Rect();
    hasLastResultTime_ = false;
    lastResultTime_ = Clock::now();
}

double FireAlarmController::consumeResultIntervalMs(TimePoint now)
{
    // 분석 FPS가 달라도 확인 시간이 실제 시간 기준으로 유지되도록 간격을 누적한다.
    double intervalMs = DEFAULT_RESULT_INTERVAL_MS;
    if (hasLastResultTime_)
        intervalMs = std::chrono::duration<double, std::milli>(now - lastResultTime_).count();

    lastResultTime_ = now;
    hasLastResultTime_ = true;
    return max(
        MIN_RESULT_INTERVAL_MS,
        min(intervalMs, MAX_RESULT_INTERVAL_MS)
    );
}

bool FireAlarmController::isSameCandidate(const cv::Rect& previous, const cv::Rect& current) const
{
    if (previous.empty() || current.empty()) return false;

    const cv::Rect intersection = previous & current;
    const double intersectionArea = intersection.empty() ? 0.0 : static_cast<double>(intersection.area());
    const double unionArea = static_cast<double>(previous.area() + current.area()) - intersectionArea;
    const double iou = unionArea > 0.0 ? intersectionArea / unionArea : 0.0;

    const cv::Point2d previousCenter(previous.x + previous.width * 0.5, previous.y + previous.height * 0.5);
    const cv::Point2d currentCenter(current.x + current.width * 0.5, current.y + current.height * 0.5);
    const double centerDistance = cv::norm(previousCenter - currentCenter);
    const double referenceSize = static_cast<double>(max(
        max(previous.width, previous.height), max(current.width, current.height)));

    // 박스 겹침이 작아도 불꽃 형태 변화로 중심점이 가까우면 같은 후보로 본다.
    return iou >= 0.10 || centerDistance <= max(30.0, referenceSize * 0.90);
}

void FireAlarmController::clearPendingEvidence()
{
    rawFireResultCount_ = 0;
    fireEvidenceMs_ = 0.0;
    activeConfirmMs_ = FINAL_CONFIRM_MS;
    activeMinRawFireResults_ = MIN_RAW_FIRE_RESULTS;
    activeAmbiguousWarmObject_ = false;
    activeCandidateBox_ = cv::Rect();
}

FireAlarmStatus FireAlarmController::processNewResult(
    const DetectionResult& result, bool resultIsFresh, TimePoint now)
{
    if (!resultIsFresh)
    {
        reset();
        return makeStatus();
    }

    const double intervalMs = consumeResultIntervalMs(now);
    const DetectionBox* primaryBox = nullptr;
    if (!result.boxes.empty())
    {
        const auto primary = std::max_element(
            result.boxes.begin(), result.boxes.end(),
            [](const DetectionBox& left, const DetectionBox& right)
            {
                return left.score < right.score;
            });
        primaryBox = &*primary;
    }
    const bool hasTrackedCandidate = !activeCandidateBox_.empty();
    const bool sameCurrentCandidate = primaryBox &&
        (activeCandidateBox_.empty() || isSameCandidate(activeCandidateBox_, primaryBox->box));

    // 검출기가 잠시 박스를 내지 않아도 내부 추적의 detected 신호는 약하게 누적한다.
    const bool detectorHoldFrame = result.detected && !primaryBox && hasTrackedCandidate;
    const bool detectorConfirmedFire = result.detected && (primaryBox || detectorHoldFrame);
    const bool detectorStrongCandidate = !detectorConfirmedFire && result.candidate && primaryBox &&
        primaryBox->strongFireEvidence && sameCurrentCandidate;

    if (detectorConfirmedFire)
    {
        if (primaryBox)
        {
            const bool sameCandidate = activeCandidateBox_.empty() ||
                isSameCandidate(activeCandidateBox_, primaryBox->box);
            if (!sameCandidate && !finalFireAlarm_) clearPendingEvidence();

            activeCandidateBox_ = primaryBox->box;
            // 손가락·반사 등 애매한 후보는 일반 화염보다 오래 확인한다.
            const bool extended = primaryBox->requiresExtendedConfirmation;
            const double requiredMs = extended ? AMBIGUOUS_CONFIRM_MS : FINAL_CONFIRM_MS;
            const int requiredResults = extended ? MIN_AMBIGUOUS_RAW_FIRE_RESULTS : MIN_RAW_FIRE_RESULTS;

            activeConfirmMs_ = max(activeConfirmMs_, requiredMs);
            activeMinRawFireResults_ = max(activeMinRawFireResults_, requiredResults);
            activeAmbiguousWarmObject_ = activeAmbiguousWarmObject_ || extended;
            fireEvidenceMs_ = min(activeConfirmMs_, fireEvidenceMs_ + intervalMs);
            rawFireResultCount_ = min(rawFireResultCount_ + 1, 1000);
        }
        else
        {
            fireEvidenceMs_ = min(activeConfirmMs_, fireEvidenceMs_ + intervalMs * DETECTOR_HOLD_GAIN);
        }
    }
    else
    {
        // 단일 음성 프레임으로 알람이 바로 꺼지지 않도록 증거를 서서히 감소시킨다.
        double decayRate = finalFireAlarm_ ? POSTCONFIRM_DECAY_RATE : PRECONFIRM_DECAY_RATE;
        if (detectorStrongCandidate)
        {
            decayRate = SOFT_CANDIDATE_DECAY_RATE;
            activeCandidateBox_ = primaryBox->box;
        }

        fireEvidenceMs_ = max(0.0, fireEvidenceMs_ - intervalMs * decayRate);
        if (!detectorStrongCandidate) rawFireResultCount_ = max(0, rawFireResultCount_ - 1);

        if (finalFireAlarm_ && fireEvidenceMs_ <= activeConfirmMs_ * RELEASE_EVIDENCE_RATIO)
        {
            reset();
            return makeStatus();
        }
        if (!finalFireAlarm_ && fireEvidenceMs_ <= 0.0) clearPendingEvidence();
    }

    updateTimers(resultIsFresh);
    return makeStatus();
}

FireAlarmStatus FireAlarmController::tick(bool resultIsFresh, TimePoint)
{
    updateTimers(resultIsFresh);
    return makeStatus();
}

void FireAlarmController::updateTimers(bool resultIsFresh)
{
    if (!resultIsFresh)
    {
        reset();
        return;
    }

    if (!finalFireAlarm_ && fireEvidenceMs_ > 0.0 &&
        rawFireResultCount_ >= activeMinRawFireResults_ && fireEvidenceMs_ >= activeConfirmMs_)
    {
        finalFireAlarm_ = true;
    }
}

FireAlarmStatus FireAlarmController::makeStatus() const
{
    FireAlarmStatus status;
    status.alarmActive = finalFireAlarm_;
    status.rawFireTiming = fireEvidenceMs_ > 0.0;
    status.ambiguousWarmObject = activeAmbiguousWarmObject_;
    status.rawFireResultCount = rawFireResultCount_;
    status.requiredRawFireResults = activeMinRawFireResults_;
    status.pendingFireMs = fireEvidenceMs_;
    status.requiredConfirmMs = activeConfirmMs_;
    return status;
}
