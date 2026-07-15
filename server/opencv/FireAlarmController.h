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

class FireAlarmController
{
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    FireAlarmController();
    FireAlarmStatus processNewResult(const DetectionResult& result, bool resultIsFresh, TimePoint now);
    FireAlarmStatus tick(bool resultIsFresh, TimePoint now);
    void reset();

private:
    static constexpr double FINAL_CONFIRM_MS = 350.0;
    static constexpr int MIN_RAW_FIRE_RESULTS = 2;
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
