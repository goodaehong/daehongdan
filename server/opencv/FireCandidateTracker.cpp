#include "FireCandidateTracker.h"

#include <algorithm>
#include <cmath>

using namespace cv;
using namespace std;

namespace
{
constexpr int FIRE_CONFIRM_FRAMES = 3;
constexpr int MAX_CANDIDATE_MISSES = 15;
constexpr int MAX_PRECONFIRM_MISSES = 3;
constexpr int STRONG_CONFIRM_FRAMES = 2;
constexpr int MAX_WEAK_KEEP_FRAMES = 12;
constexpr int PERSISTENCE_HOLD_FRAMES = 45;
constexpr double NEW_TRACK_MIN_SCORE = 0.42;
constexpr double STRONG_FIRE_SCORE = 0.65;
constexpr double KEEP_FIRE_SCORE = 0.59;
}

void FireCandidateTracker::reset()
{
    fireConfirmCount_ = candidateMissCount_ = strongFireCount_ = weakKeepCount_ = 0;
    persistenceHoldCount_ = 0;
    fireConfirmed_ = false;
    previousCandidateBox_ = Rect();
}

bool FireCandidateTracker::hasTrustedTrack() const
{
    return !previousCandidateBox_.empty() &&
        (
            fireConfirmed_ ||
            persistenceHoldCount_ > 0 ||
            (fireConfirmCount_ >= 3 && strongFireCount_ >= 2)
        );
}

Rect FireCandidateTracker::trackedBox() const { return previousCandidateBox_; }

double FireCandidateTracker::calculateIoU(const Rect& a, const Rect& b) const
{
    if (a.empty() || b.empty()) return 0.0;
    const Rect intersection = a & b;
    if (intersection.empty()) return 0.0;
    const double intersectionArea = static_cast<double>(intersection.area());
    const double unionArea = static_cast<double>(a.area() + b.area()) - intersectionArea;
    return unionArea > 0.0 ? intersectionArea / unionArea : 0.0;
}

bool FireCandidateTracker::isSameCandidate(const Rect& previous, const Rect& current) const
{
    if (previous.empty() || current.empty()) return false;

    const Point2d previousCenter(previous.x + previous.width * 0.5, previous.y + previous.height * 0.5);
    const Point2d currentCenter(current.x + current.width * 0.5, current.y + current.height * 0.5);
    const double centerDistance = norm(previousCenter - currentCenter);
    const double referenceSize = static_cast<double>(max(
        max(previous.width, previous.height), max(current.width, current.height)));

    return calculateIoU(previous, current) >= 0.15 ||
        centerDistance <= max(20.0, referenceSize * 0.75);
}

FireTrackingResult FireCandidateTracker::update(const vector<DetectionBox>& acceptedBoxes)
{
    FireTrackingResult result;
    const DetectionBox* highest = nullptr;
    const DetectionBox* tracked = nullptr;
    const DetectionBox* best = nullptr;

    const auto continuesTrack = [&](const DetectionBox& candidate)
    {
        return !previousCandidateBox_.empty() && isSameCandidate(previousCandidateBox_, candidate.box);
    };

    const auto newTrackScore = [](const DetectionBox& candidate)
    {
        if (candidate.fingerLikeCandidate) return 0.92;
        if (candidate.reflectionLikeCandidate) return 0.70;
        if (candidate.tinyCandidate) return 0.55;
        return NEW_TRACK_MIN_SCORE;
    };

    for (const DetectionBox& candidate : acceptedBoxes)
    {
        const bool continuing = continuesTrack(candidate);
        if (!continuing && candidate.score < newTrackScore(candidate)) continue;
        if (!highest || candidate.score > highest->score) highest = &candidate;
        if (continuing && (!tracked || candidate.score > tracked->score)) tracked = &candidate;
    }

    constexpr double TRACK_SWITCH_SCORE_MARGIN = 0.12;
    if (tracked && highest)
        best = highest->score >= tracked->score + TRACK_SWITCH_SCORE_MARGIN ? highest : tracked;
    else
        best = tracked ? tracked : highest;

    int requiredConfirmFrames = FIRE_CONFIRM_FRAMES;
    int requiredStrongFrames = STRONG_CONFIRM_FRAMES;

    if (best)
    {
        requiredConfirmFrames = best->fingerLikeCandidate ? 9 :
            best->reflectionLikeCandidate ? 7 : best->tinyCandidate ? 4 : FIRE_CONFIRM_FRAMES;
        requiredStrongFrames = best->fingerLikeCandidate ? 6 :
            best->reflectionLikeCandidate ? 4 : best->tinyCandidate ? 2 : STRONG_CONFIRM_FRAMES;

        const bool sameTrack = isSameCandidate(previousCandidateBox_, best->box);
        if (sameTrack)
        {
            const int limit = max(FIRE_CONFIRM_FRAMES + 4, requiredConfirmFrames + 2);
            fireConfirmCount_ = min(fireConfirmCount_ + 1, limit);
        }
        else
        {
            fireConfirmCount_ = 1;
            strongFireCount_ = weakKeepCount_ = 0;
            fireConfirmed_ = false;
        }
        candidateMissCount_ = 0;

        if (best->strongFireEvidence &&
            !best->fingerLikeCandidate &&
            !best->reflectionLikeCandidate)
        {
            persistenceHoldCount_ = PERSISTENCE_HOLD_FRAMES;
        }
        else
        {
            persistenceHoldCount_ = max(0, persistenceHoldCount_ - 1);
        }

        const bool tinySkinOk = !best->tinyCandidate || !best->skinLikeCandidate ||
            best->skinSeparatedFlameEvidence;
        const bool reflectionRescue = best->reflectionLikeCandidate && best->score >= 0.86 &&
            best->redOrangeRatio >= 0.24 &&
            (best->brightnessDiffMean >= 2.8 || best->maskChangeRatio >= 0.040);
        const bool reflectionOk = !best->reflectionLikeCandidate || best->coreHaloEvidence ||
            best->brightBackgroundEvidence || reflectionRescue;
        const bool fingerOk = !best->fingerLikeCandidate || best->skinSeparatedFlameEvidence;

        if (!fireConfirmed_)
        {
            const double strongScore = best->fingerLikeCandidate ? 0.92 :
                best->reflectionLikeCandidate ? 0.82 : best->tinyCandidate ? 0.72 : STRONG_FIRE_SCORE;

            if (best->score >= strongScore && best->strongFireEvidence &&
                tinySkinOk && reflectionOk && fingerOk)
                strongFireCount_ = min(strongFireCount_ + 1, max(8, requiredStrongFrames + 2));
            else
                strongFireCount_ = max(0, strongFireCount_ - 1);

            if (fireConfirmCount_ >= requiredConfirmFrames && strongFireCount_ >= requiredStrongFrames)
            {
                fireConfirmed_ = true;
                weakKeepCount_ = 0;
            }
        }
        else
        {
            const double keepScore = best->fingerLikeCandidate ? 0.86 :
                best->reflectionLikeCandidate ? 0.74 : best->tinyCandidate ? 0.66 : KEEP_FIRE_SCORE;
            const bool reflectionKeepRescue = best->reflectionLikeCandidate && best->score >= 0.78 &&
                best->redOrangeRatio >= 0.22 &&
                (best->brightnessDiffMean >= 2.2 || best->maskChangeRatio >= 0.032);
            const bool reflectionKeepOk = !best->reflectionLikeCandidate || best->coreHaloEvidence ||
                best->brightBackgroundEvidence || reflectionKeepRescue;
            const bool stableCoreHalo = best->coreHaloEvidence && best->strongFireEvidence &&
                !best->fingerLikeCandidate && !best->reflectionLikeCandidate &&
                best->score >= max(0.45, keepScore - 0.10);

            const bool stableTrackedColor = best->trackedPersistenceEvidence &&
                best->strongFireEvidence &&
                !best->fingerLikeCandidate &&
                !best->reflectionLikeCandidate &&
                best->score >= max(0.43, keepScore - 0.18);

            if ((best->score >= keepScore || stableCoreHalo || stableTrackedColor) &&
                best->strongFireEvidence &&
                tinySkinOk && reflectionKeepOk && fingerOk)
                weakKeepCount_ = 0;
            else if (++weakKeepCount_ > MAX_WEAK_KEEP_FRAMES)
            {
                fireConfirmed_ = false;
                fireConfirmCount_ = strongFireCount_ = weakKeepCount_ = 0;
            }
        }

        previousCandidateBox_ = best->box;
    }
    else if (!fireConfirmed_)
    {
        ++candidateMissCount_;
        persistenceHoldCount_ = max(0, persistenceHoldCount_ - 1);

        if (candidateMissCount_ <= MAX_PRECONFIRM_MISSES &&
            !previousCandidateBox_.empty() && fireConfirmCount_ > 0)
        {
            fireConfirmCount_ = max(0, fireConfirmCount_ - 1);
            strongFireCount_ = max(0, strongFireCount_ - 1);
            weakKeepCount_ = 0;
        }
        else
        {
            fireConfirmCount_ = candidateMissCount_ = strongFireCount_ = weakKeepCount_ = 0;

            if (persistenceHoldCount_ <= 0)
                previousCandidateBox_ = Rect();
        }
    }
    else
    {
        ++candidateMissCount_;
        persistenceHoldCount_ = max(0, persistenceHoldCount_ - 1);

        if (candidateMissCount_ > MAX_CANDIDATE_MISSES)
        {
            fireConfirmed_ = false;
            fireConfirmCount_ = candidateMissCount_ = strongFireCount_ = weakKeepCount_ = 0;

            if (persistenceHoldCount_ <= 0)
                previousCandidateBox_ = Rect();
        }
    }

    result.candidate = best != nullptr;
    result.detected = fireConfirmed_ && candidateMissCount_ <= MAX_CANDIDATE_MISSES;
    result.candidateDisplayReady = best && !best->fingerLikeCandidate && !result.detected &&
        fireConfirmCount_ >= max(1, requiredConfirmFrames - 1) &&
        strongFireCount_ >= max(1, requiredStrongFrames - 1);
    result.hitCount = result.confirmCount = fireConfirmCount_;
    if (best) result.boxes.push_back(*best);
    return result;
}
