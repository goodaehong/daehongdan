#pragma once

#include <chrono>

#include "DetectionTypes.h"

struct FireAlarmStatus
{
    bool alarmActive = false;
    bool rawFireTiming = false;
    bool ambiguousWarmObject = false;

    int rawFireResultCount = 0;
    int requiredRawFireResults = 2;

    double pendingFireMs = 0.0;
    double requiredConfirmMs = 350.0;
};

// FireDetector가 만든 프레임 단위 결과를 시간적으로 재검증하여
// 실제 UI/사이렌에 사용할 최종 화재 경보를 결정한다.
class FireAlarmController
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    FireAlarmController();

    // 서로 다른 새 검출 결과가 도착했을 때 한 번만 호출한다.
    FireAlarmStatus processNewResult(
        const DetectionResult& result,
        bool resultIsFresh,
        TimePoint now
    );

    // 새 결과가 없는 동안에도 확인/해제 타이머가 진행되도록 호출한다.
    FireAlarmStatus tick(
        bool resultIsFresh,
        TimePoint now
    );

    void reset();

private:
    static constexpr double FINAL_CONFIRM_MS = 350.0;
    static constexpr double FINAL_RELEASE_MS = 350.0;
    static constexpr int MIN_RAW_FIRE_RESULTS = 2;

    static constexpr double AMBIGUOUS_CONFIRM_MS = 900.0;
    static constexpr int MIN_AMBIGUOUS_RAW_FIRE_RESULTS = 3;
    static constexpr double AMBIGUOUS_MIN_SCORE = 0.78;

private:
    bool finalFireAlarm_ = false;
    bool rawFireTiming_ = false;
    bool rawFireLostTiming_ = false;

    int rawFireResultCount_ = 0;

    double activeConfirmMs_ = FINAL_CONFIRM_MS;
    int activeMinRawFireResults_ = MIN_RAW_FIRE_RESULTS;
    bool activeAmbiguousWarmObject_ = false;

    TimePoint rawFireStartTime_;
    TimePoint rawFireLostStartTime_;

private:
    void updateTimers(bool resultIsFresh, TimePoint now);
    FireAlarmStatus makeStatus(TimePoint now) const;
};
