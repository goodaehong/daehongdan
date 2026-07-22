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

void FlameDetector::reset()
{
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

    // 4채널 환경에서는 채널 자체가 병렬이므로 검출기 내부 중첩 병렬화를 피한다.
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
    features.vStd = stdV[0];

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

    double score =
        0.20 * clamp01(f.colorCoverage / 0.70) +
        0.14 * clamp01(f.motionCoverage / 0.70) +
        0.14 * clamp01(f.redOrangeCoverage / 0.45) +
        0.10 * clamp01(f.whiteCoreCoverage / 0.12) +
        0.10 * clamp01(f.vStd / 55.0) +
        0.07 * f.circularity +
        0.07 * clamp01(f.solidity) +
        0.04 * clamp01(f.extent / 0.70) +
        0.06 * clamp01(f.textureEntropy / 3.0) +
        0.08 * clamp01(f.maskChange / 0.30);

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

vector<DetectionBox> FlameDetector::updateTracks(const vector<Features>& detections)
{
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

DetectionResult FlameDetector::detect(const Mat& inputFrame)
{
    DetectionResult result;
    if (inputFrame.empty()) return result;

    Mat frame;
    const double scale = std::min(
        static_cast<double>(flame_config::ANALYSIS_WIDTH) / inputFrame.cols,
        static_cast<double>(flame_config::ANALYSIS_HEIGHT) / inputFrame.rows);

    // 입력이 360p 이하이면 확대하지 않고 원본 픽셀을 그대로 사용한다.
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

    vector<Mat> hsvChannels;
    split(hsv, hsvChannels);
    const Mat& hue = hsvChannels[0];
    const Mat& value = hsvChannels[2];

    Mat motionMask = buildMotionMask(frame, gray);
    Mat colorMask = buildOriginalColorMask(frame, motionMask);
    Mat skinMask = buildSkinMask(frame, hsv);
    Mat whiteCoreMask = buildWhiteCoreMask(hsv, colorMask);

    Mat candidateMask;
    bitwise_and(colorMask, motionMask, candidateMask);
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

    vector<Features> accepted;
    const size_t limit = min(contours.size(), static_cast<size_t>(flame_config::MAX_CONTOURS_TO_ANALYZE));
    for (size_t i = 0; i < limit; ++i)
    {
        if (contourArea(contours[i]) < flame_config::MIN_CONTOUR_AREA) continue;
        Features features = analyzeContour(
            contours[i], gray, hue, value, colorMask, motionMask,
            candidateMask, skinMask, whiteCoreMask);
        if (features.box.empty() || features.score < flame_config::NEW_TRACK_MIN_SCORE) continue;
        accepted.push_back(features);
    }

    if (frameIndex_ >= flame_config::BACKGROUND_WARMUP_FRAMES)
        result.boxes = updateTracks(accepted);

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
    result.debugImages.fireColorMask = colorMask;
    result.debugImages.skinMask = skinMask;
    result.debugImages.foregroundMask = motionMask;
    result.debugImages.candidateMask = candidateMask;
#endif

    gray.copyTo(previousGray_);
    candidateMask.copyTo(previousCandidateMask_);
    frameIndex_++;
    return result;
}