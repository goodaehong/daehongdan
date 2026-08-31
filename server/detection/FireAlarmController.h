#pragma once

// 화재 알람 확정. 검출이 연속으로 쌓여야 알람을 켜고, 한두 번 끊겨도 바로 끄지 않는다.
// 검출이 깜빡일 때 경보가 함께 요동치는 것을 막기 위한 계층이다.


#include <chrono>

#include "DetectionTypes.h"

struct FireAlarmStatus
{
    bool alarmActive = false;
    bool rawFireTiming = false;
    bool ambiguousWarmObject = false;
    int rawFireResultCount = 0;
    int requiredRawFireResults = 1;
    double pendingFireMs = 0.0;
    double requiredConfirmMs = 120.0;
};

// FlameDetector가 확정한 결과에 짧은 히스테리시스를 적용해 최종 화재 알람을 만든다.
// 애매한 따뜻한 색 물체는 더 긴 확인 시간과 더 많은 결과를 요구한다.
class FireAlarmController
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    FireAlarmController();
    FireAlarmStatus processNewResult(
        const DetectionResult& result,
        bool resultIsFresh,
        TimePoint now
    );
    FireAlarmStatus tick(bool resultIsFresh, TimePoint now);
    void reset();

private:
    // 일반 화염은 검출기 내부 추적을 이미 통과했으므로 추가 확인을 짧게 유지한다.
    static constexpr double FINAL_CONFIRM_MS = 120.0;
    static constexpr int MIN_RAW_FIRE_RESULTS = 1;
    static constexpr double AMBIGUOUS_CONFIRM_MS = 900.0;
    static constexpr int MIN_AMBIGUOUS_RAW_FIRE_RESULTS = 3;
    static constexpr double DEFAULT_RESULT_INTERVAL_MS = 33.0;
    static constexpr double MIN_RESULT_INTERVAL_MS = 10.0;
    static constexpr double MAX_RESULT_INTERVAL_MS = 250.0;
    static constexpr double PRECONFIRM_DECAY_RATE = 0.55;
    static constexpr double SOFT_CANDIDATE_DECAY_RATE = 0.12;
    static constexpr double POSTCONFIRM_DECAY_RATE = 0.45;
    static constexpr double RELEASE_EVIDENCE_RATIO = 0.20;
    static constexpr double DETECTOR_HOLD_GAIN = 0.20;

    bool finalFireAlarm_ = false;
    int rawFireResultCount_ = 0;
    double fireEvidenceMs_ = 0.0;
    double activeConfirmMs_ = FINAL_CONFIRM_MS;
    int activeMinRawFireResults_ = MIN_RAW_FIRE_RESULTS;
    bool activeAmbiguousWarmObject_ = false;
    cv::Rect activeCandidateBox_;
    bool hasLastResultTime_ = false;
    TimePoint lastResultTime_;

    double consumeResultIntervalMs(TimePoint now);
    bool isSameCandidate(const cv::Rect& previous, const cv::Rect& current) const;
    void clearPendingEvidence();
    void updateTimers(bool resultIsFresh);
    FireAlarmStatus makeStatus() const;
};
