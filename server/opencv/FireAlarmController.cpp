#include "FireAlarmController.h"

#include <algorithm>

using std::max;

FireAlarmController::FireAlarmController()
{
    reset();
}

void FireAlarmController::reset()
{
    finalFireAlarm_ = false;
    rawFireTiming_ = false;
    rawFireLostTiming_ = false;
    rawFireResultCount_ = 0;

    activeConfirmMs_ = FINAL_CONFIRM_MS;
    activeMinRawFireResults_ = MIN_RAW_FIRE_RESULTS;
    activeAmbiguousWarmObject_ = false;

    const TimePoint now = Clock::now();
    rawFireStartTime_ = now;
    rawFireLostStartTime_ = now;
}

FireAlarmStatus FireAlarmController::processNewResult(
    const DetectionResult& result,
    bool resultIsFresh,
    TimePoint now
)
{
    const DetectionBox* primaryBox =
        !result.boxes.empty()
        ? &result.boxes.front()
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
        result.detected &&
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

        if (!rawFireTiming_)
        {
            rawFireTiming_ = true;
            rawFireStartTime_ = now;
            rawFireResultCount_ = 1;
            activeConfirmMs_ = requiredConfirmMs;
            activeMinRawFireResults_ = requiredRawResults;
            activeAmbiguousWarmObject_ = ambiguousWarmObject;
        }
        else
        {
            ++rawFireResultCount_;

            activeConfirmMs_ =
                max(activeConfirmMs_, requiredConfirmMs);

            activeMinRawFireResults_ =
                max(activeMinRawFireResults_, requiredRawResults);

            activeAmbiguousWarmObject_ =
                activeAmbiguousWarmObject_ || ambiguousWarmObject;
        }

        // 다시 화염 결과가 들어오면 해제 타이머를 취소한다.
        rawFireLostTiming_ = false;
    }
    else
    {
        if (!finalFireAlarm_)
        {
            rawFireTiming_ = false;
            rawFireResultCount_ = 0;
            activeConfirmMs_ = FINAL_CONFIRM_MS;
            activeMinRawFireResults_ = MIN_RAW_FIRE_RESULTS;
            activeAmbiguousWarmObject_ = false;
        }
        else if (!rawFireLostTiming_)
        {
            rawFireLostTiming_ = true;
            rawFireLostStartTime_ = now;
        }
    }

    updateTimers(resultIsFresh, now);
    return makeStatus(now);
}

FireAlarmStatus FireAlarmController::tick(
    bool resultIsFresh,
    TimePoint now
)
{
    updateTimers(resultIsFresh, now);
    return makeStatus(now);
}

void FireAlarmController::updateTimers(
    bool resultIsFresh,
    TimePoint now
)
{
    double pendingFireMs = 0.0;

    if (rawFireTiming_)
    {
        pendingFireMs =
            std::chrono::duration<double, std::milli>(
                now - rawFireStartTime_
            ).count();
    }

    if (!finalFireAlarm_ &&
        rawFireTiming_ &&
        rawFireResultCount_ >= activeMinRawFireResults_ &&
        pendingFireMs >= activeConfirmMs_)
    {
        finalFireAlarm_ = true;
        rawFireLostTiming_ = false;
    }

    if (finalFireAlarm_ && rawFireLostTiming_)
    {
        const double lostFireMs =
            std::chrono::duration<double, std::milli>(
                now - rawFireLostStartTime_
            ).count();

        if (lostFireMs >= FINAL_RELEASE_MS)
        {
            reset();
        }
    }

    // 오래된 검출 결과로 경보 상태를 계속 유지하지 않는다.
    if (!resultIsFresh)
    {
        reset();
    }
}

FireAlarmStatus FireAlarmController::makeStatus(TimePoint now) const
{
    FireAlarmStatus status;

    status.alarmActive = finalFireAlarm_;
    status.rawFireTiming = rawFireTiming_;
    status.ambiguousWarmObject = activeAmbiguousWarmObject_;
    status.rawFireResultCount = rawFireResultCount_;
    status.requiredRawFireResults = activeMinRawFireResults_;
    status.requiredConfirmMs = activeConfirmMs_;

    if (rawFireTiming_)
    {
        status.pendingFireMs =
            std::chrono::duration<double, std::milli>(
                now - rawFireStartTime_
            ).count();
    }

    return status;
}