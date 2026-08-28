#include "FlameDetector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "AppConfig.h"

using namespace cv;
using namespace std;

namespace
{
    double clamp01(double value)
    {
        return std::max(0.0, std::min(1.0, value));
    }

    double safeRatio(double value, double total)
    {
        return total > 0.0 ? value / total : 0.0;
    }

    Rect clampRect(const Rect& rect, const Size& size)
    {
        return rect & Rect(0, 0, size.width, size.height);
    }

    double calculateRelativeBrightnessScore(
        double candidateBrightness,
        double surroundingBrightness)
    {
        if (surroundingBrightness < 0.0)
            return 0.0;

        const double delta = candidateBrightness - surroundingBrightness;
        const double ratio =
            (candidateBrightness + 8.0) /
            (surroundingBrightness + 8.0);

        // 어두운 장면의 실제 불꽃은 주변보다 뚜렷하게 밝아야 한다.
        if (surroundingBrightness < 80.0)
        {
            return
                0.55 * clamp01((delta - 6.0) / 34.0) +
                0.45 * clamp01((ratio - 1.06) / 0.34);
        }

        // 일반 밝기에서는 국부 대비 조건을 조금 완화한다.
        if (surroundingBrightness < 170.0)
        {
            return
                0.55 * clamp01((delta - 3.0) / 30.0) +
                0.45 * clamp01((ratio - 1.03) / 0.25);
        }

        // 밝은 장면은 WDR과 노출로 불꽃 대비가 줄 수 있어 약한 가점만 계산한다.
        return
            0.60 * clamp01((delta + 2.0) / 28.0) +
            0.40 * clamp01((ratio - 0.99) / 0.18);
    }
}

FlameDetector::FlameDetector()
{
    kernel3_ = getStructuringElement(MORPH_ELLIPSE, Size(3, 3));
    kernel5_ = getStructuringElement(MORPH_ELLIPSE, Size(5, 5));
    kernel7_ = getStructuringElement(MORPH_ELLIPSE, Size(7, 7));
    reset();

    if (flame_config::USE_OPTIONAL_SVM)
    {
        try
        {
            svm_ = Algorithm::load<ml::SVM>(flame_config::OPTIONAL_SVM_PATH);
            svmReady_ = !svm_.empty();
        }
        catch (const cv::Exception&)
        {
            svmReady_ = false;
        }
    }
}

void FlameDetector::setIgnoreRegionConfig(const IgnoreRegionConfig& config)
{
    ignoreRegionFilter_.setConfig(config);

    // 설정 변경 전에 누적된 동일 위치 트랙이 경보로 이어지지 않게 한다.
    // 배경 모델은 유지하므로 영상 분석 자체를 다시 워밍업하지는 않는다.
    tracks_.clear();
}

void FlameDetector::reset()
{
    // 스트림이 바뀌면 배경 모델과 이전 프레임 및 추적을 함께 초기화한다.
    mog2_ = createBackgroundSubtractorMOG2(300, 16.0, false);
    mog2_->setDetectShadows(false);
    previousGray_.release();
    previousCandidateMask_.release();
    tracks_.clear();
    frameIndex_ = 0;
    nextTrackId_ = 1;
}

Mat FlameDetector::buildMotionMask(const Mat& frame, const Mat& gray)
{
    Mat motion;

    // MOG2 배경 차분과 직전 프레임 차분을 합쳐 느린 변화와 빠른 깜빡임을 모두 잡는다.
    if (flame_config::MOG2_SCALE < 0.999)
    {
        Mat smallGray;
        resize(gray, smallGray, Size(), flame_config::MOG2_SCALE, flame_config::MOG2_SCALE, INTER_AREA);
        Mat smallMotion;
        mog2_->apply(smallGray, smallMotion, flame_config::MOG2_LEARNING_RATE);
        threshold(smallMotion, smallMotion, 200, 255, THRESH_BINARY);
        resize(smallMotion, motion, gray.size(), 0, 0, INTER_NEAREST);
    }
    else
    {
        mog2_->apply(gray, motion, flame_config::MOG2_LEARNING_RATE);
        threshold(motion, motion, 200, 255, THRESH_BINARY);
    }

    if (!previousGray_.empty() && previousGray_.size() == gray.size())
    {
        Mat diff;
        absdiff(previousGray_, gray, diff);
        threshold(diff, diff, 12, 255, THRESH_BINARY);
        bitwise_or(motion, diff, motion);
    }

    morphologyEx(motion, motion, MORPH_OPEN, kernel3_);
    morphologyEx(motion, motion, MORPH_CLOSE, kernel5_);
    return motion;
}

Mat FlameDetector::buildOriginalColorMask(const Mat& frame, const Mat& motionMask) const
{
    Mat blurred;
    GaussianBlur(frame, blurred, Size(3, 3), 0.0);
    Mat color = Mat::zeros(frame.size(), CV_8UC1);

    // 채널 단위 실행을 직렬화하므로 내부 픽셀 루프는 추가 병렬화하지 않는다.
    for (int y = 0; y < frame.rows; ++y)
    {
        const Vec3b* src = blurred.ptr<Vec3b>(y);
        const uchar* movement = motionMask.ptr<uchar>(y);
        uchar* dst = color.ptr<uchar>(y);

        for (int x = 0; x < frame.cols; ++x)
        {
            if (movement[x] == 0) continue;

            const int b = src[x][0];
            const int g = src[x][1];
            const int r = src[x][2];
            const int sum = b + g + r;
            if (sum <= 0) continue;

            const int minimum = std::min(b, std::min(g, r));
            const double saturation = 1.0 - (3.0 * minimum / sum);
            const double requiredSaturation =
                (255.0 - r) * flame_config::ORIGINAL_SATURATION_COEFFICIENT /
                flame_config::ORIGINAL_RED_THRESHOLD;

            if (r > flame_config::ORIGINAL_RED_THRESHOLD &&
                r >= g && g > b && saturation >= requiredSaturation)
            {
                dst[x] = 255;
            }
        }
    }

    morphologyEx(color, color, MORPH_OPEN, kernel3_);
    morphologyEx(color, color, MORPH_CLOSE, kernel7_);
    medianBlur(color, color, 3);
    return color;
}

Mat FlameDetector::buildSkinMask(const Mat& frame, const Mat& hsv) const
{
    // YCrCb와 HSV 조건을 모두 만족한 영역만 피부로 보아 손 오검출을 줄인다.
    Mat ycrcb;
    cvtColor(frame, ycrcb, COLOR_BGR2YCrCb);

    Mat skinYCrCb, skinHSV, skin;
    inRange(ycrcb, Scalar(0, 133, 77), Scalar(255, 180, 135), skinYCrCb);
    inRange(hsv, Scalar(0, 20, 45), Scalar(25, 230, 255), skinHSV);
    bitwise_and(skinYCrCb, skinHSV, skin);
    morphologyEx(skin, skin, MORPH_CLOSE, kernel5_);
    dilate(skin, skin, kernel3_);
    return skin;
}

Mat FlameDetector::buildWhiteCoreMask(const Mat& hsv, const Mat& colorMask) const
{
    // 화염색 주변의 저채도·고명도 영역만 흰 불꽃 중심부로 인정한다.
    Mat white, halo, core;
    inRange(hsv, Scalar(0, 0, 220), Scalar(179, 75, 255), white);
    dilate(colorMask, halo, kernel7_);
    bitwise_and(white, halo, core);
    return core;
}

void FlameDetector::calculateGlcm(
    const Mat& gray,
    const Mat& mask,
    double& entropy,
    double& energy) const
{
    // 후보의 거친 질감과 불규칙성을 표현하는 축소 GLCM 특징을 계산한다.
    entropy = 0.0;
    energy = 0.0;
    if (gray.empty() || mask.empty() || countNonZero(mask) < 8) return;

    Mat smallGray = gray;
    Mat smallMask = mask;
    const int maxSide = std::max(gray.cols, gray.rows);
    if (maxSide > 96)
    {
        const double scale = 96.0 / maxSide;
        resize(gray, smallGray, Size(), scale, scale, INTER_AREA);
        resize(mask, smallMask, smallGray.size(), 0, 0, INTER_NEAREST);
    }

    constexpr int LEVELS = 8;
    double matrix[LEVELS][LEVELS] = {};
    double pairCount = 0.0;

    for (int y = 0; y < smallGray.rows; ++y)
    {
        const uchar* g = smallGray.ptr<uchar>(y);
        const uchar* m = smallMask.ptr<uchar>(y);
        for (int x = 0; x + 1 < smallGray.cols; ++x)
        {
            if (m[x] == 0 || m[x + 1] == 0) continue;
            const int a = std::min(LEVELS - 1, g[x] * LEVELS / 256);
            const int b = std::min(LEVELS - 1, g[x + 1] * LEVELS / 256);
            matrix[a][b] += 1.0;
            matrix[b][a] += 1.0;
            pairCount += 2.0;
        }
    }

    if (pairCount <= 0.0) return;
    for (int i = 0; i < LEVELS; ++i)
    {
        for (int j = 0; j < LEVELS; ++j)
        {
            const double p = matrix[i][j] / pairCount;
            if (p <= 0.0) continue;
            entropy -= p * std::log(p);
            energy += p * p;
        }
    }
}

FlameDetector::Features FlameDetector::analyzeContour(
    const vector<Point>& contour,
    const Mat& gray,
    const Mat& hue,
    const Mat& value,
    const Mat& colorMask,
    const Mat& motionMask,
    const Mat& candidateMask,
    const Mat& skinMask,
    const Mat& whiteCoreMask) const
{
    Features features;
    Rect box = clampRect(boundingRect(contour), gray.size());
    if (box.empty()) return features;

    const double area = contourArea(contour);
    const double perimeter = arcLength(contour, true);
    if (area <= 0.0 || perimeter <= 0.0) return features;

    vector<Point> localContour;
    localContour.reserve(contour.size());
    for (const Point& p : contour)
        localContour.emplace_back(p.x - box.x, p.y - box.y);

    Mat component = Mat::zeros(box.size(), CV_8UC1);
    vector<vector<Point>> localContours(1, localContour);
    drawContours(component, localContours, 0, Scalar(255), FILLED);
    const double componentPixels = countNonZero(component);
    if (componentPixels <= 0.0) return features;

    Mat localColor, localMotion, localSkin, localWhite;
    bitwise_and(colorMask(box), component, localColor);
    bitwise_and(motionMask(box), component, localMotion);
    bitwise_and(skinMask(box), component, localSkin);
    bitwise_and(whiteCoreMask(box), component, localWhite);

    features.box = box;
    features.colorCoverage = safeRatio(countNonZero(localColor), componentPixels);
    features.motionCoverage = safeRatio(countNonZero(localMotion), componentPixels);
    features.whiteCoreCoverage = safeRatio(countNonZero(localWhite), componentPixels);
    features.skinCoverage = safeRatio(countNonZero(localSkin), componentPixels);

    const Mat roiHue = hue(box);
    const Mat roiValue = value(box);
    Mat hueRed1, hueRed2, hueOrange, redOrange;
    inRange(roiHue, Scalar(0), Scalar(15), hueRed1);
    inRange(roiHue, Scalar(170), Scalar(179), hueRed2);
    inRange(roiHue, Scalar(16), Scalar(30), hueOrange);
    bitwise_or(hueRed1, hueRed2, redOrange);
    bitwise_or(redOrange, hueOrange, redOrange);
    bitwise_and(redOrange, component, redOrange);
    features.redOrangeCoverage = safeRatio(countNonZero(redOrange), componentPixels);

    Scalar meanV, stdV;
    meanStdDev(roiValue, meanV, stdV, component);
    features.candidateBrightness = meanV[0];
    features.vStd = stdV[0];

    // HSV V 채널에서 후보 바깥 링을 배경으로 잡아 국부 밝기 차이를 계산한다.
    // 가능한 경우 링의 화염색 픽셀은 제외해 큰 불꽃이 자기 자신과 비교되지 않게 한다.
    const int ringX = max(4, cvRound(box.width * 0.35));
    const int ringY = max(4, cvRound(box.height * 0.35));
    const Rect expanded = clampRect(
        Rect(
            box.x - ringX,
            box.y - ringY,
            box.width + ringX * 2,
            box.height + ringY * 2),
        value.size());

    if (!expanded.empty() && expanded.area() > box.area())
    {
        Mat ringMask(expanded.size(), CV_8UC1, Scalar(255));
        const Rect innerBox(
            box.x - expanded.x,
            box.y - expanded.y,
            box.width,
            box.height);
        rectangle(ringMask, innerBox, Scalar(0), FILLED);

        Mat ringMaskWithoutFire;
        Mat notFire;
        bitwise_not(colorMask(expanded), notFire);
        bitwise_and(ringMask, notFire, ringMaskWithoutFire);

        // 유효 배경이 너무 적은 화면 가장자리에서는 전체 링을 대신 사용한다.
        const int usableBackgroundPixels = countNonZero(ringMaskWithoutFire);
        const Mat& selectedRingMask =
            usableBackgroundPixels >= 12 ? ringMaskWithoutFire : ringMask;

        if (countNonZero(selectedRingMask) >= 8)
        {
            features.surroundingBrightness =
                mean(value(expanded), selectedRingMask)[0];
            features.brightnessDelta =
                features.candidateBrightness - features.surroundingBrightness;
            features.brightnessRatio =
                (features.candidateBrightness + 8.0) /
                (features.surroundingBrightness + 8.0);
            features.relativeBrightnessScore =
                calculateRelativeBrightnessScore(
                    features.candidateBrightness,
                    features.surroundingBrightness);
        }
    }

    vector<Point> hull;
    convexHull(contour, hull);
    const double hullArea = hull.size() >= 3 ? contourArea(hull) : 0.0;
    const double hullPerimeter = hull.size() >= 3 ? arcLength(hull, true) : perimeter;

    features.circularity = clamp01(4.0 * CV_PI * area / (perimeter * perimeter));
    features.solidity = safeRatio(area, hullArea);
    features.extent = safeRatio(area, static_cast<double>(box.area()));
    features.roughness = clamp01(hullPerimeter / perimeter);

    calculateGlcm(gray(box), component, features.textureEntropy, features.textureEnergy);

    if (!previousCandidateMask_.empty() && previousCandidateMask_.size() == candidateMask.size())
    {
        Mat currentLocal, previousLocal, changed;
        bitwise_and(candidateMask(box), component, currentLocal);
        bitwise_and(previousCandidateMask_(box), component, previousLocal);
        bitwise_xor(currentLocal, previousLocal, changed);
        features.maskChange = safeRatio(countNonZero(changed), componentPixels);
    }

    features.score = classify(features);
    return features;
}

Mat FlameDetector::Features::svmRow() const
{
    // 기존 SVM XML 호환을 위해 입력은 원래의 13개 특징으로 유지한다.
    return (Mat_<float>(1, 13) <<
        static_cast<float>(colorCoverage),
        static_cast<float>(motionCoverage),
        static_cast<float>(redOrangeCoverage),
        static_cast<float>(whiteCoreCoverage),
        static_cast<float>(skinCoverage),
        static_cast<float>(vStd / 64.0),
        static_cast<float>(circularity),
        static_cast<float>(solidity),
        static_cast<float>(extent),
        static_cast<float>(roughness),
        static_cast<float>(textureEntropy / 4.0),
        static_cast<float>(textureEnergy),
        static_cast<float>(maskChange));
}

double FlameDetector::classify(const Features& f) const
{
    if (svmReady_)
    {
        const float prediction = svm_->predict(f.svmRow());
        if (prediction <= 0.0f) return 0.0;
    }

    // 움직이는 노란 물체의 오검출을 줄이기 위해 움직임·형태보다 흰 중심부와
    // 시간에 따른 마스크 변화에 더 큰 비중을 둔다.
    double score =
        0.20 * clamp01(f.colorCoverage / 0.70) +
        0.06 * clamp01(f.motionCoverage / 0.70) +
        0.14 * clamp01(f.redOrangeCoverage / 0.45) +
        0.15 * clamp01(f.whiteCoreCoverage / 0.12) +
        0.10 * clamp01(f.vStd / 55.0) +
        0.03 * f.circularity +
        0.03 * clamp01(f.solidity) +
        0.02 * clamp01(f.extent / 0.70) +
        0.08 * clamp01(f.textureEntropy / 3.0) +
        0.10 * clamp01(f.maskChange / 0.30);

    // 작은 밝은 불꽃이 형태 점수 때문에 누락되지 않도록 절대 밝기에 최대 0.11을 더한다.
    const double absoluteBrightnessScore =
        clamp01((f.candidateBrightness - 165.0) / 65.0);
    score += 0.11 * absoluteBrightnessScore;

    // 흰 중심부 없이 어둡고 단단하게 움직이는 적·주황 물체는 관측된 오검출 유형이다.
    const bool dimMovingBlob =
        f.surroundingBrightness >= 0.0 &&
        f.surroundingBrightness < 170.0 &&
        f.candidateBrightness < 190.0 &&
        f.redOrangeCoverage >= 0.55 &&
        f.whiteCoreCoverage < 0.015 &&
        f.motionCoverage >= 0.65 &&
        f.solidity >= 0.75 &&
        f.extent >= 0.50;

    if (f.surroundingBrightness >= 0.0)
    {
        // 어두운 모니터나 벽 앞의 물체가 대비만으로 점수를 얻지 않게 제한한다.
        if (!dimMovingBlob)
            score += 0.04 * f.relativeBrightnessScore;

        // 주변보다 밝지 않은 후보에는 작은 감점을 주되 밝은 배경에는 적용하지 않는다.
        if (f.surroundingBrightness < 170.0 &&
            f.brightnessDelta < 2.0 &&
            f.brightnessRatio < 1.02 &&
            f.whiteCoreCoverage < 0.020)
        {
            score -= 0.03 * clamp01((2.0 - f.brightnessDelta) / 20.0);
        }
    }

    if (dimMovingBlob)
        score -= 0.18;

    // 충분히 밝으면서 흰 중심부와 적·주황 영역이 함께 있을 때만 화염 가점을 준다.
    const bool brightCoreFlameEvidence =
        !dimMovingBlob &&
        f.candidateBrightness >= 200.0 &&
        f.whiteCoreCoverage >= 0.010 &&
        f.redOrangeCoverage >= 0.18;

    if (brightCoreFlameEvidence)
        score += 0.05;

#if FLAME_ENABLE_SKIN_REJECTION
    const bool independentFlameStructure =
        f.whiteCoreCoverage >= 0.025 ||
        (f.redOrangeCoverage >= 0.30 && f.vStd >= 24.0 && f.maskChange >= 0.035);

    if (f.skinCoverage >= 0.60 && !independentFlameStructure)
        return 0.0;

    score -= 0.35 * clamp01(f.skinCoverage / 0.70);
#endif

    return clamp01(score);
}

double FlameDetector::intersectionOverUnion(const Rect& a, const Rect& b)
{
    if (a.empty() || b.empty()) return 0.0;
    const Rect intersection = a & b;
    const double intersectionArea = intersection.empty() ? 0.0 : intersection.area();
    const double unionArea = static_cast<double>(a.area() + b.area()) - intersectionArea;
    return unionArea > 0.0 ? intersectionArea / unionArea : 0.0;
}

bool FlameDetector::sameTarget(const Rect& a, const Rect& b)
{
    if (a.empty() || b.empty()) return false;
    if (intersectionOverUnion(a, b) >= 0.12) return true;

    const Point2d ca(a.x + a.width * 0.5, a.y + a.height * 0.5);
    const Point2d cb(b.x + b.width * 0.5, b.y + b.height * 0.5);
    const double distance = norm(ca - cb);
    const double reference = max({ a.width, a.height, b.width, b.height, 20 });
    return distance <= reference * 0.75;
}

vector<DetectionBox> FlameDetector::updateTracks(
    const vector<Features>& detections,
    std::uint64_t frameId,
    std::int64_t timestampMs)
{
    // 각 기존 트랙에 위치와 점수를 함께 고려한 최적 후보 하나를 연결한다.
    vector<bool> detectionUsed(detections.size(), false);

    for (Track& track : tracks_)
    {
        int bestIndex = -1;
        double bestValue = -numeric_limits<double>::infinity();
        for (size_t i = 0; i < detections.size(); ++i)
        {
            if (detectionUsed[i] || !sameTarget(track.box, detections[i].box)) continue;
            const double value = intersectionOverUnion(track.box, detections[i].box) + detections[i].score;
            if (value > bestValue)
            {
                bestValue = value;
                bestIndex = static_cast<int>(i);
            }
        }

        if (bestIndex >= 0)
        {
            const Features& detection = detections[bestIndex];
            detectionUsed[bestIndex] = true;
            track.box = detection.box;
            track.score = detection.score;
            track.hits++;
            track.misses = 0;
            if (detection.score >= flame_config::CONFIRM_MIN_SCORE)
                track.strongHits++;
            else
                track.strongHits = max(0, track.strongHits - 1);

            track.areaHistory.push_back(static_cast<double>(detection.box.area()));
            if (track.areaHistory.size() > 16) track.areaHistory.pop_front();
            track.colorCoverage = detection.colorCoverage;
            track.motionCoverage = detection.motionCoverage;
            track.redOrangeCoverage = detection.redOrangeCoverage;
            track.whiteCoreCoverage = detection.whiteCoreCoverage;
            track.skinCoverage = detection.skinCoverage;
            track.vStd = detection.vStd;
            track.maskChange = detection.maskChange;

            // 연속 관측 횟수와 강한 점수 횟수를 모두 만족해야 화염으로 확정한다.
            if (track.hits >= flame_config::CONFIRM_HITS && track.strongHits >= 2)
                track.confirmed = true;
        }
        else
        {
            track.misses++;
        }
    }

    for (size_t i = 0; i < detections.size(); ++i)
    {
        if (detectionUsed[i] || detections[i].score < flame_config::NEW_TRACK_MIN_SCORE) continue;
        Track track;
        track.id = nextTrackId_++;
        track.box = detections[i].box;
        track.firstBox = detections[i].box;
        track.firstSeenFrameId = frameId;
        track.firstSeenTimestampMs = timestampMs;
        track.hits = 1;
        track.strongHits = detections[i].score >= flame_config::CONFIRM_MIN_SCORE ? 1 : 0;
        track.score = detections[i].score;
        track.areaHistory.push_back(static_cast<double>(track.box.area()));
        track.colorCoverage = detections[i].colorCoverage;
        track.motionCoverage = detections[i].motionCoverage;
        track.redOrangeCoverage = detections[i].redOrangeCoverage;
        track.whiteCoreCoverage = detections[i].whiteCoreCoverage;
        track.skinCoverage = detections[i].skinCoverage;
        track.vStd = detections[i].vStd;
        track.maskChange = detections[i].maskChange;
        tracks_.push_back(track);
    }

    tracks_.erase(
        remove_if(tracks_.begin(), tracks_.end(), [](const Track& track)
            {
                return track.misses > flame_config::MAX_TRACK_MISSES;
            }),
        tracks_.end());

    vector<DetectionBox> result;
    for (const Track& track : tracks_)
    {
        if (!track.confirmed || track.misses > 1) continue;

        DetectionBox box;
        box.box = track.box;
        box.trackId = track.id;
        box.type = DetectionType::FIRE;
        box.score = track.score;

        box.strongFireEvidence = track.score >= 0.58;
        box.tinyCandidate = track.box.area() < flame_config::TINY_CANDIDATE_AREA;
        box.skinLikeCandidate = track.skinCoverage >= 0.35;
        box.coreHaloEvidence = track.whiteCoreCoverage >= 0.025 && track.redOrangeCoverage >= 0.12;
        box.skinSeparatedFlameEvidence = box.coreHaloEvidence ||
            (track.redOrangeCoverage >= 0.30 && track.vStd >= 24.0 && track.maskChange >= 0.035);
        box.requiresExtendedConfirmation =
            box.skinLikeCandidate && !box.skinSeparatedFlameEvidence;
        box.trackedPersistenceEvidence = track.misses > 0;
        box.brightnessDiffMean = track.vStd;
        box.maskChangeRatio = track.maskChange;
        box.redOrangeRatio = track.redOrangeCoverage;

        char label[80];
        std::snprintf(label, sizeof(label), "FIRE %.2f", track.score);
        box.label = label;
        result.push_back(box);
    }
    return result;
}

DetectionResult FlameDetector::detect(
    const Mat& inputFrame,
    std::uint64_t frameId,
    std::int64_t timestampMs)
{
    DetectionResult result;
    if (inputFrame.empty()) return result;

    Mat frame;
    const double scale = std::min(
        static_cast<double>(flame_config::ANALYSIS_WIDTH) / inputFrame.cols,
        static_cast<double>(flame_config::ANALYSIS_HEIGHT) / inputFrame.rows);

    // 360p 이하 입력은 확대하지 않고 큰 입력만 분석 크기로 축소한다.
    if (scale < 0.999)
    {
        const Size analysisSize(
            std::max(1, cvRound(inputFrame.cols * scale)),
            std::max(1, cvRound(inputFrame.rows * scale)));
        resize(inputFrame, frame, analysisSize, 0, 0, INTER_AREA);
    }
    else
    {
        frame = inputFrame;
    }

    Mat gray, hsv;
    cvtColor(frame, gray, COLOR_BGR2GRAY);
    cvtColor(frame, hsv, COLOR_BGR2HSV);

#if FIRE_DEBUG_VIEW
    Mat ycrcb;
    cvtColor(frame, ycrcb, COLOR_BGR2YCrCb);
#endif

    vector<Mat> hsvChannels;
    split(hsv, hsvChannels);
    const Mat& hue = hsvChannels[0];
    const Mat& value = hsvChannels[2];

    // 움직이는 화염색을 기본 후보로 만들고 주변의 밝은 중심부를 보완한다.
    Mat motionMask = buildMotionMask(frame, gray);
    Mat colorMask = buildOriginalColorMask(frame, motionMask);
    Mat skinMask = buildSkinMask(frame, hsv);
    Mat whiteCoreMask = buildWhiteCoreMask(hsv, colorMask);

    Mat candidateMask;
    bitwise_and(colorMask, motionMask, candidateMask);
#if FIRE_DEBUG_VIEW
    const Mat combinedMask = candidateMask.clone();
#endif
    Mat expandedColor, coreHalo;
    dilate(colorMask, expandedColor, kernel7_);
    bitwise_and(whiteCoreMask, expandedColor, coreHalo);
    bitwise_or(candidateMask, coreHalo, candidateMask);
    morphologyEx(candidateMask, candidateMask, MORPH_CLOSE, kernel7_);
    medianBlur(candidateMask, candidateMask, 3);

    vector<vector<Point>> contours;
    Mat contourInput = candidateMask.clone();
    findContours(contourInput, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    sort(contours.begin(), contours.end(), [](const vector<Point>& a, const vector<Point>& b)
        {
            return contourArea(a) > contourArea(b);
        });

#if FIRE_DEBUG_VIEW
    Mat contourOverlay = frame.clone();
    for (const vector<Point>& contour : contours)
    {
        if (contourArea(contour) < flame_config::MIN_CONTOUR_AREA) continue;
        drawContours(contourOverlay, vector<vector<Point>>{ contour }, -1,
            Scalar(203, 79, 244), 2, LINE_AA);
    }
    Mat featureScoreOverlay = frame.clone();
#endif

    vector<Features> accepted;
    const size_t limit = min(contours.size(), static_cast<size_t>(flame_config::MAX_CONTOURS_TO_ANALYZE));
    for (size_t i = 0; i < limit; ++i)
    {
        if (contourArea(contours[i]) < flame_config::MIN_CONTOUR_AREA) continue;
        Features features = analyzeContour(
            contours[i], gray, hue, value, colorMask, motionMask,
            candidateMask, skinMask, whiteCoreMask);
#if FIRE_DEBUG_VIEW
        if (!features.box.empty())
        {
            const bool passes = features.score >= flame_config::NEW_TRACK_MIN_SCORE;
            const Scalar color = passes ? Scalar(70, 205, 90) : Scalar(125, 125, 125);
            rectangle(featureScoreOverlay, features.box, color, 2, LINE_AA);
            const int barWidth = std::max(1, cvRound(features.box.width * clamp01(features.score)));
            const int barY = std::max(2, features.box.y - 7);
            rectangle(featureScoreOverlay,
                Rect(features.box.x, barY, std::min(barWidth, features.box.width), 5),
                color, FILLED, LINE_AA);
        }
#endif
        if (features.box.empty() || features.score < flame_config::NEW_TRACK_MIN_SCORE) continue;
        if (ignoreRegionFilter_.shouldIgnore(features.box, frame.size())) continue;
        accepted.push_back(features);
    }

    // MOG2 초기 배경 학습 전에는 후보를 계산하되 추적 결과를 외부로 내보내지 않는다.
    if (frameIndex_ >= flame_config::BACKGROUND_WARMUP_FRAMES)
        result.boxes = updateTracks(
            accepted, frameId, timestampMs);

#if FIRE_DEBUG_VIEW
    Mat trackingOverlay = frame.clone();
    for (const Track& track : tracks_)
    {
        if (track.misses > 1 || track.box.empty()) continue;
        const Scalar color = track.confirmed ? Scalar(40, 40, 230) : Scalar(60, 170, 255);
        rectangle(trackingOverlay, track.box, color, 2, LINE_AA);
        const double progress = clamp01(
            static_cast<double>(track.hits) / std::max(1, flame_config::CONFIRM_HITS));
        const int progressWidth = std::max(1, cvRound(track.box.width * progress));
        const int progressY = std::min(frame.rows - 6, track.box.y + track.box.height + 3);
        rectangle(trackingOverlay,
            Rect(track.box.x, progressY, std::min(progressWidth, track.box.width), 4),
            color, FILLED, LINE_AA);
    }

    Mat confirmedOverlay = frame.clone();
    for (const DetectionBox& box : result.boxes)
        rectangle(confirmedOverlay, box.box, Scalar(35, 35, 230), 3, LINE_AA);
#endif

    result.candidate = !accepted.empty();
    result.detected = !result.boxes.empty();
    result.candidateDisplayReady = result.candidate;

    double totalArea = 0.0;
    int maxHits = 0;
    for (const Track& track : tracks_)
    {
        if (track.misses <= 1)
            maxHits = max(maxHits, track.hits);
    }

    const double scaleX = static_cast<double>(inputFrame.cols) / frame.cols;
    const double scaleY = static_cast<double>(inputFrame.rows) / frame.rows;
    for (DetectionBox& box : result.boxes)
    {
        box.box = Rect(
            cvRound(box.box.x * scaleX),
            cvRound(box.box.y * scaleY),
            max(1, cvRound(box.box.width * scaleX)),
            max(1, cvRound(box.box.height * scaleY))) &
            Rect(0, 0, inputFrame.cols, inputFrame.rows);

        box.normalizedX = std::clamp(
            static_cast<double>(box.box.x) / std::max(1, inputFrame.cols), 0.0, 1.0);
        box.normalizedY = std::clamp(
            static_cast<double>(box.box.y) / std::max(1, inputFrame.rows), 0.0, 1.0);
        box.normalizedWidth = std::clamp(
            static_cast<double>(box.box.width) / std::max(1, inputFrame.cols), 0.0, 1.0);
        box.normalizedHeight = std::clamp(
            static_cast<double>(box.box.height) / std::max(1, inputFrame.rows), 0.0, 1.0);

        const double representativePixelX =
            static_cast<double>(box.box.x) +
            static_cast<double>(std::max(0, box.box.width - 1)) * 0.5;
        const double representativePixelY =
            static_cast<double>(box.box.y + std::max(0, box.box.height - 1));
        box.representativePositionValid = true;
        box.representativeNormalizedX = std::clamp(
            representativePixelX / std::max(1, inputFrame.cols - 1), 0.0, 1.0);
        box.representativeNormalizedY = std::clamp(
            representativePixelY / std::max(1, inputFrame.rows - 1), 0.0, 1.0);
        totalArea += box.box.area();
    }

    result.area = totalArea;
    result.hitCount = maxHits;
    result.confirmCount = flame_config::CONFIRM_HITS;
    result.flicker = any_of(accepted.begin(), accepted.end(), [](const Features& f)
        {
            return f.maskChange >= 0.04;
        });

#if FIRE_DEBUG_VIEW
    result.debugImages.analysisFrame = frame;
    result.debugImages.grayImage = gray;
    result.debugImages.hsvImage = hsv;
    result.debugImages.yCrCbImage = ycrcb;
    result.debugImages.fireColorMask = colorMask;
    result.debugImages.skinMask = skinMask;
    result.debugImages.foregroundMask = motionMask;
    result.debugImages.combinedMask = combinedMask;
    result.debugImages.candidateMask = candidateMask;
    result.debugImages.contourOverlay = contourOverlay;
    result.debugImages.featureScoreOverlay = featureScoreOverlay;
    result.debugImages.trackingOverlay = trackingOverlay;
    result.debugImages.confirmedOverlay = confirmedOverlay;
#endif

    gray.copyTo(previousGray_);
    candidateMask.copyTo(previousCandidateMask_);
    frameIndex_++;
    return result;
}
