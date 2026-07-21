#include "FireDetector_1.h"
#include "AppConfig.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <utility>

using namespace cv;
using namespace std;

namespace
{
    constexpr int DETECT_WIDTH = 960;
    constexpr int DETECT_HEIGHT = 540;
    constexpr std::size_t MAX_CONTOURS_TO_CHECK = 20;
    constexpr double MIN_CONTOUR_AREA = 10.0;

    double clamp01(double value) { return max(0.0, min(1.0, value)); }
    double safeRatio(double value, double total) { return total > 0.0 ? value / total : 0.0; }
    double rectArea(const Rect& box) { return static_cast<double>(box.width) * box.height; }

    Mat makeComponentMask(const vector<Point>& contour, const Rect& box)
    {
        vector<Point> local;
        local.reserve(contour.size());
        for (const Point& point : contour) local.emplace_back(point.x - box.x, point.y - box.y);

        Mat mask = Mat::zeros(box.size(), CV_8UC1);
        drawContours(mask, vector<vector<Point>>{ local }, 0, Scalar(255), FILLED);
        return mask;
    }
}

FireDetector::FireDetector() = default;

void FireDetector::reset()
{
    maskBuilder_.reset();
    candidateTracker_.reset();
    prevVal.release();
    prevFireMask.release();
    frameIndex = 0;
}

DetectionResult FireDetector::detect(const Mat& inputFrame)
{
    DetectionResult result;
    if (inputFrame.empty()) return result;

    Mat frame, gray, hsv;
    resize(inputFrame, frame, Size(DETECT_WIDTH, DETECT_HEIGHT), 0, 0, INTER_LINEAR);
    const double scaleX = static_cast<double>(inputFrame.cols) / frame.cols;
    const double scaleY = static_cast<double>(inputFrame.rows) / frame.rows;

    cvtColor(frame, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, gray, Size(3, 3), 0.0);
    cvtColor(frame, hsv, COLOR_BGR2HSV);

    vector<Mat> hsvCh;
    split(hsv, hsvCh);
    const Mat& hue = hsvCh[0];
    const Mat& sat = hsvCh[1];
    const Mat& val = hsvCh[2];

    Mat skinMask = maskBuilder_.makeSkinMask(frame, hsv);
    Mat fireColorMask = maskBuilder_.makeFireColorMask(frame, hsv);
    Mat foregroundMask = maskBuilder_.makeForegroundMask(frame, gray, frameIndex);

    // ( AND ) +  core-halo +      
    Mat strongWhiteCore, expandedCore, strongCoreHaloMask;
    inRange(hsv, Scalar(0, 0, 226), Scalar(179, 64, 255), strongWhiteCore);
    bitwise_and(strongWhiteCore, fireColorMask, strongWhiteCore);
    dilate(strongWhiteCore, expandedCore, getStructuringElement(MORPH_ELLIPSE, Size(9, 9)));
    bitwise_and(fireColorMask, expandedCore, strongCoreHaloMask);
    bitwise_or(strongCoreHaloMask, strongWhiteCore, strongCoreHaloMask);
    maskBuilder_.cleanupBinaryMask(strongCoreHaloMask, 1, 3, 1);

    Mat movingFireCandidate;
    bitwise_and(fireColorMask, foregroundMask, movingFireCandidate);

    Mat trackedPersistence = Mat::zeros(frame.size(), CV_8UC1);
    if (candidateTracker_.hasTrustedTrack())
    {
        const Rect original = candidateTracker_.trackedBox();
        Rect tracked(
            cvRound(original.x / scaleX), cvRound(original.y / scaleY),
            max(1, cvRound(original.width / scaleX)), max(1, cvRound(original.height / scaleY)));

        const int padX = max(8, cvRound(tracked.width * 0.35));
        const int padY = max(8, cvRound(tracked.height * 0.35));
        tracked = Rect(tracked.x - padX, tracked.y - padY,
            tracked.width + padX * 2, tracked.height + padY * 2) & Rect(0, 0, frame.cols, frame.rows);

        if (!tracked.empty())
        {
            Mat region = Mat::zeros(frame.size(), CV_8UC1);
            rectangle(region, tracked, Scalar(255), FILLED);
            bitwise_and(fireColorMask, region, trackedPersistence);
            maskBuilder_.cleanupBinaryMask(trackedPersistence, 1, 3, 1);
        }
    }

    Mat rawCandidateMask, candidateMask;
    bitwise_or(movingFireCandidate, strongCoreHaloMask, rawCandidateMask);
    bitwise_or(rawCandidateMask, trackedPersistence, rawCandidateMask);
    candidateMask = rawCandidateMask.clone();
    maskBuilder_.cleanupBinaryMask(candidateMask, 3, 3, 3);

    vector<vector<Point>> contours;
    Mat contourMask = candidateMask.clone();
    findContours(contourMask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    sort(contours.begin(), contours.end(), [](const vector<Point>& a, const vector<Point>& b)
        {
            return contourArea(a) > contourArea(b);
        });

    bool hasFlicker = false;
    double totalFireArea = 0.0;
    vector<DetectionBox> acceptedBoxes;
    const std::size_t contourLimit = min(contours.size(), MAX_CONTOURS_TO_CHECK);

    for (std::size_t index = 0; index < contourLimit; ++index)
    {
        // Declare mutable temporaries at the beginning of the block.
        // This avoids MSVC source-encoding/parser cascades around later declarations.
        cv::Mat skinOverlapMask;
        double brightnessDiffMean = 0.0;
        double maskChangeRatio = 0.0;

        const vector<Point>& contour = contours[index];
        const double contourAreaValue = contourArea(contour);
        if (contourAreaValue < MIN_CONTOUR_AREA) continue;

        Rect box = boundingRect(contour) & Rect(0, 0, frame.cols, frame.rows);
        if (box.empty() || box.width < 3 || box.height < 3) continue;

        const double boxArea = rectArea(box);
        if (boxArea <= 0.0) continue;

        const bool smallRegion = boxArea < 900.0;
        const int shortSide = max(1, min(box.width, box.height));
        const int longSide = max(box.width, box.height);
        if (static_cast<double>(longSide) / shortSide > 5.5) continue;

        vector<Point> hull;
        convexHull(contour, hull);
        const double hullArea = hull.size() >= 3 ? contourArea(hull) : 0.0;
        const double solidity = safeRatio(contourAreaValue, hullArea);

        Mat componentMask = makeComponentMask(contour, box);
        if (countNonZero(componentMask) <= 0) continue;

        const Mat roiFire = fireColorMask(box);
        const Mat roiForeground = foregroundMask(box);
        const Mat roiCandidate = rawCandidateMask(box);
        const Mat roiCoreHalo = strongCoreHaloMask(box);
        const Mat roiTracked = trackedPersistence(box);
        const Mat roiH = hue(box);
        const Mat roiS = sat(box);
        const Mat roiV = val(box);
        const Mat roiSkin = skinMask(box);

        Mat componentFire, componentForeground, componentCandidate;
        Mat componentCoreHalo, componentTracked, componentSkin;
        bitwise_and(roiFire, componentMask, componentFire);
        bitwise_and(roiForeground, componentMask, componentForeground);
        bitwise_and(roiCandidate, componentMask, componentCandidate);
        bitwise_and(roiCoreHalo, componentMask, componentCoreHalo);
        bitwise_and(roiTracked, componentMask, componentTracked);
        bitwise_and(roiSkin, componentMask, componentSkin);

        const int firePixelCount = countNonZero(componentFire);
        const int foregroundPixelCount = countNonZero(componentForeground);
        const int candidatePixelCount = countNonZero(componentCandidate);
        const int coreHaloPixelCount = countNonZero(componentCoreHalo);
        const int trackedPixelCount = countNonZero(componentTracked);

        const bool hasCoreHaloBypass = coreHaloPixelCount >= (smallRegion ? 4 : 8) &&
            safeRatio(coreHaloPixelCount, boxArea) >= (smallRegion ? 0.004 : 0.008);
        const bool hasTrackedPersistence = trackedPixelCount >= (smallRegion ? 6 : 12) &&
            safeRatio(trackedPixelCount, boxArea) >= (smallRegion ? 0.008 : 0.012);

        if (firePixelCount <= 0 || candidatePixelCount <= 0 ||
            (foregroundPixelCount <= 0 && !hasCoreHaloBypass && !hasTrackedPersistence)) continue;

        const double colorRatio = safeRatio(firePixelCount, boxArea);
        const double foregroundRatio = safeRatio(foregroundPixelCount, boxArea);
        const double candidateRatio = safeRatio(candidatePixelCount, boxArea);

        if (smallRegion)
        {
            if (colorRatio < 0.015 || candidateRatio < 0.004) continue;
        }
        else if (colorRatio < 0.035 || candidateRatio < 0.010 ||
            (foregroundRatio < 0.010 && !hasCoreHaloBypass && !hasTrackedPersistence))
        {
            continue;
        }

        //  
        Mat redHue1, redHue2, redHue, orangeHue, yellowHue, yellowOrangeHue;
        inRange(roiH, Scalar(0), Scalar(15), redHue1);
        inRange(roiH, Scalar(170), Scalar(179), redHue2);
        bitwise_or(redHue1, redHue2, redHue);
        inRange(roiH, Scalar(16), Scalar(28), orangeHue);
        inRange(roiH, Scalar(29), Scalar(45), yellowHue);
        inRange(roiH, Scalar(20), Scalar(45), yellowOrangeHue);

        Mat satStrong, valBright;
        compare(roiS, 65, satStrong, CMP_GT);
        compare(roiV, 120, valBright, CMP_GT);

        Mat redOrangeFire, pureRedFire, yellowObject, yellowOrangeObject;
        bitwise_or(redHue, orangeHue, redOrangeFire);
        bitwise_and(redOrangeFire, satStrong, redOrangeFire);
        bitwise_and(redOrangeFire, valBright, redOrangeFire);
        bitwise_and(redOrangeFire, componentFire, redOrangeFire);

        bitwise_and(redHue, satStrong, pureRedFire);
        bitwise_and(pureRedFire, valBright, pureRedFire);
        bitwise_and(pureRedFire, componentFire, pureRedFire);

        bitwise_and(yellowHue, satStrong, yellowObject);
        bitwise_and(yellowObject, valBright, yellowObject);
        bitwise_and(yellowObject, componentForeground, yellowObject);

        bitwise_and(yellowOrangeHue, satStrong, yellowOrangeObject);
        bitwise_and(yellowOrangeObject, valBright, yellowOrangeObject);
        bitwise_and(yellowOrangeObject, componentForeground, yellowOrangeObject);

        Mat whiteCoreFire;
        inRange(hsv(box), Scalar(0, 0, 221), Scalar(179, 64, 255), whiteCoreFire);
        bitwise_and(whiteCoreFire, componentFire, whiteCoreFire);

        const int redOrangePixelCount = countNonZero(redOrangeFire);
        const int pureRedPixelCount = countNonZero(pureRedFire);
        const int whiteCorePixelCount = countNonZero(whiteCoreFire);

        Mat whiteCoreDilated, whiteCoreRing, inverseCore, haloRedOrange;
        dilate(whiteCoreFire, whiteCoreDilated, getStructuringElement(MORPH_ELLIPSE, Size(9, 9)));
        bitwise_not(whiteCoreFire, inverseCore);
        bitwise_and(whiteCoreDilated, inverseCore, whiteCoreRing);
        bitwise_and(whiteCoreRing, componentMask, whiteCoreRing);
        bitwise_and(whiteCoreRing, redOrangeFire, haloRedOrange);

        const int haloRingPixelCount = countNonZero(whiteCoreRing);
        const int haloRedOrangePixelCount = countNonZero(haloRedOrange);

        Mat haloSkin, nonSkinMask, haloNonSkin;
        bitwise_and(haloRedOrange, componentSkin, haloSkin);
        bitwise_not(componentSkin, nonSkinMask);
        bitwise_and(haloRedOrange, nonSkinMask, haloNonSkin);

        const int haloSkinPixelCount = countNonZero(haloSkin);
        const int haloNonSkinPixelCount = countNonZero(haloNonSkin);
        const double haloSupportRatio = safeRatio(haloRedOrangePixelCount, haloRingPixelCount);
        const double haloSkinRatio = safeRatio(haloSkinPixelCount, haloRedOrangePixelCount);
        const double nonSkinHaloRatio = safeRatio(haloNonSkinPixelCount, haloRedOrangePixelCount);

        const bool skinSeparatedHalo = haloNonSkinPixelCount >= 4 && nonSkinHaloRatio >= 0.30;
        const bool coreHaloEvidence = whiteCorePixelCount >= 3 &&
            haloRedOrangePixelCount >= 6 && haloSupportRatio >= 0.16;

        const double redOrangeRatio = safeRatio(redOrangePixelCount, firePixelCount);
        const double pureRedRatio = safeRatio(pureRedPixelCount, firePixelCount);
        const double yellowObjectRatio = safeRatio(countNonZero(yellowObject), foregroundPixelCount);
        const double yellowDominantRatio = safeRatio(countNonZero(yellowOrangeObject), foregroundPixelCount);
        const double whiteCoreRatio = safeRatio(whiteCorePixelCount, firePixelCount);

        Scalar meanV, stdV, meanS, stdS;
        meanStdDev(roiV, meanV, stdV, componentFire);
        meanStdDev(roiS, meanS, stdS, componentFire);
        const double vStd = stdV[0];
        const double sStd = stdS[0];

        //  
        bitwise_and(componentSkin, componentCandidate, skinOverlapMask);
        const double candidateSkinRatio = safeRatio(
            countNonZero(skinOverlapMask), candidatePixelCount);

        const int contextPadX = max(6, box.width / 2);
        const int contextPadY = max(6, box.height / 2);
        const int contextX = max(0, box.x - contextPadX);
        const int contextY = max(0, box.y - contextPadY);
        Rect contextBox(contextX, contextY,
            min(frame.cols, box.x + box.width + contextPadX) - contextX,
            min(frame.rows, box.y + box.height + contextPadY) - contextY);

        Mat contextRing = Mat::ones(contextBox.size(), CV_8UC1) * 255;
        Rect localBox(box.x - contextBox.x, box.y - contextBox.y, box.width, box.height);
        localBox &= Rect(0, 0, contextRing.cols, contextRing.rows);
        if (!localBox.empty()) contextRing(localBox).setTo(0);

        Mat surroundingSkin;
        bitwise_and(skinMask(contextBox), contextRing, surroundingSkin);
        const double surroundingSkinRatio = safeRatio(countNonZero(surroundingSkin), countNonZero(contextRing));

        const bool tinyCandidate = boxArea < 1500.0 || min(box.width, box.height) < 28 || firePixelCount < 100;
        const bool connectedSkin = tinyCandidate && candidateSkinRatio >= 0.20 && surroundingSkinRatio >= 0.08;
        const bool reliableWhiteCore = whiteCorePixelCount >= (smallRegion ? 3 : 6) &&
            redOrangePixelCount >= (smallRegion ? 8 : 16) &&
            whiteCoreRatio >= (smallRegion ? 0.020 : 0.030);
        const bool reliablePureRed = pureRedPixelCount >= (smallRegion ? 4 : 8) &&
            redOrangePixelCount >= (smallRegion ? 8 : 16) &&
            pureRedRatio >= (smallRegion ? 0.070 : 0.100);
        const bool hasStrongFlameCore = reliableWhiteCore || reliablePureRed;

        if (connectedSkin && candidateSkinRatio > 0.60 && !hasStrongFlameCore && redOrangeRatio < 0.35)
            continue;

        double skinPenalty = 0.0;
        if (connectedSkin && candidateSkinRatio > 0.50 && !hasStrongFlameCore) skinPenalty = 0.12;
        else if (connectedSkin && candidateSkinRatio > 0.35 && whiteCoreRatio < 0.015 && pureRedRatio < 0.050)
            skinPenalty = 0.06;

        if (redOrangeRatio <= (smallRegion ? 0.04 : 0.08) &&
            whiteCoreRatio <= (smallRegion ? 0.010 : 0.020)) continue;

        //    
        const bool yellowDominant = yellowDominantRatio > (smallRegion ? 0.58 : 0.48);
        const bool weakFlameLayer = pureRedRatio < (smallRegion ? 0.045 : 0.060) &&
            whiteCoreRatio < (smallRegion ? 0.018 : 0.025);
        const bool uniformRigidYellow = solidity > 0.78 && vStd < 32.0 && sStd < 38.0;

        if (yellowDominant && weakFlameLayer && uniformRigidYellow && boxArea >= 220.0) continue;
        if (yellowObjectRatio > 0.35 && redOrangeRatio < 0.22 && whiteCoreRatio < 0.080) continue;
        if (!smallRegion && yellowObjectRatio > 0.25 && redOrangeRatio < 0.28 && whiteCoreRatio < 0.100)
            continue;
        if (!smallRegion && yellowObjectRatio > 0.18 && vStd < 35.0 && whiteCoreRatio < 0.100)
            continue;

        //    
        if (!prevVal.empty() && !prevFireMask.empty() &&
            prevVal.size() == val.size() && prevFireMask.size() == fireColorMask.size())
        {
            Mat valueDiff, previousComponentFire, maskXor;
            absdiff(roiV, prevVal(box), valueDiff);
            brightnessDiffMean = mean(valueDiff, componentFire)[0];
            bitwise_and(prevFireMask(box), componentMask, previousComponentFire);
            bitwise_xor(componentFire, previousComponentFire, maskXor);
            maskChangeRatio = safeRatio(countNonZero(maskXor),
                max(1, firePixelCount + countNonZero(previousComponentFire)));
        }

        const double dynamicScore = 0.5 * clamp01(brightnessDiffMean / 18.0) +
            0.5 * clamp01(maskChangeRatio / 0.18);
        const bool dynamicEnough = smallRegion
            ? dynamicScore > 0.07 || brightnessDiffMean > 1.7 || maskChangeRatio > 0.020
            : dynamicScore > 0.15 || brightnessDiffMean > 3.5 || maskChangeRatio > 0.050;

        const bool stableCoreHalo = hasCoreHaloBypass && coreHaloEvidence &&
            whiteCorePixelCount >= (smallRegion ? 3 : 6) &&
            redOrangePixelCount >= (smallRegion ? 6 : 12) && !connectedSkin;

        // A real flame can become weak in the foreground and frame-difference masks after
        // it remains at one position. Inside an already trusted track, keep the candidate
        // when a non-uniform fire-color structure remains. Motion is not mandatory here.
        const bool persistentColorStructure =
            coreHaloEvidence ||
            reliablePureRed ||
            reliableWhiteCore ||
            (
                redOrangeRatio >= (smallRegion ? 0.26 : 0.30) &&
                vStd >= (smallRegion ? 18.0 : 22.0) &&
                sStd >= (smallRegion ? 14.0 : 18.0)
            );

        const bool stableTrackedFire =
            hasTrackedPersistence &&
            !connectedSkin &&
            !yellowDominant &&
            redOrangePixelCount >= (smallRegion ? 6 : 12) &&
            redOrangeRatio >= (smallRegion ? 0.16 : 0.20) &&
            persistentColorStructure;

        if (frameIndex > 3 && !dynamicEnough && !stableCoreHalo && !stableTrackedFire) continue;
        if (!smallRegion && vStd < 18.0 && sStd < 20.0 && whiteCoreRatio < 0.080) continue;

        const bool hasColorLayer = reliablePureRed || reliableWhiteCore;
        const bool complexVariation = !yellowDominant && vStd >= (smallRegion ? 24.0 : 30.0) &&
            (maskChangeRatio >= (smallRegion ? 0.045 : 0.070) ||
                brightnessDiffMean >= (smallRegion ? 3.0 : 5.0));
        const bool skinSafeBrightBackground = !connectedSkin || candidateSkinRatio < 0.48 ||
            coreHaloEvidence || skinSeparatedHalo;
        const bool brightBackground = meanV[0] >= 185.0 &&
            redOrangePixelCount >= (smallRegion ? 5 : 10) && skinSafeBrightBackground &&
            (maskChangeRatio >= (smallRegion ? 0.055 : 0.080) ||
                brightnessDiffMean >= (smallRegion ? 3.5 : 5.5));

        const bool reflectionLike = whiteCoreRatio >= (smallRegion ? 0.10 : 0.14) &&
            !coreHaloEvidence && !reliablePureRed &&
            pureRedRatio < (smallRegion ? 0.045 : 0.060) &&
            redOrangeRatio < (smallRegion ? 0.32 : 0.36) &&
            vStd < (smallRegion ? 30.0 : 34.0) && !complexVariation && !brightBackground;

        const bool strictSkinSeparated = skinSeparatedHalo &&
            haloNonSkinPixelCount >= (smallRegion ? 5 : 10) &&
            ((coreHaloEvidence && reliableWhiteCore && redOrangePixelCount >= (smallRegion ? 10 : 18)) ||
                (reliablePureRed && complexVariation));
        const bool independentFlame =
            (coreHaloEvidence && reliablePureRed &&
                (complexVariation || brightnessDiffMean >= 3.2 || maskChangeRatio >= 0.045)) ||
            (reliableWhiteCore && reliablePureRed && complexVariation) ||
            (brightBackground && candidateSkinRatio < 0.20);
        const bool skinConnected = connectedSkin && (haloSkinRatio >= 0.55 || surroundingSkinRatio >= 0.12);
        const bool skinSeparatedFlame = strictSkinSeparated ||
            (candidateSkinRatio < 0.20 && independentFlame);
        const bool fingerLike = tinyCandidate && skinConnected;

        const bool reflectionRescue = reflectionLike &&
            redOrangePixelCount >= (smallRegion ? 6 : 12) &&
            redOrangeRatio >= (smallRegion ? 0.24 : 0.28) &&
            (brightnessDiffMean >= (smallRegion ? 2.8 : 4.0) ||
                maskChangeRatio >= (smallRegion ? 0.040 : 0.060));
        const bool fingerRescue = fingerLike && strictSkinSeparated && reliableWhiteCore &&
            (reliablePureRed || complexVariation);

        bool strongFireEvidence = hasColorLayer || complexVariation || coreHaloEvidence ||
            brightBackground || stableTrackedFire || reflectionRescue || fingerRescue;
        if (connectedSkin && !skinSeparatedFlame) strongFireEvidence = false;
        if (tinyCandidate && !hasColorLayer && !coreHaloEvidence && !stableTrackedFire)
            strongFireEvidence = false;

        double yellowPenalty = 0.0;
        if (yellowDominant && weakFlameLayer) yellowPenalty = uniformRigidYellow ? 0.22 : 0.12;
        else if (yellowDominantRatio > 0.40 && pureRedRatio < 0.040 && whiteCoreRatio < 0.015)
            yellowPenalty = 0.07;

        double finalScore =
            0.20 * clamp01(colorRatio / 0.20) +
            0.30 * clamp01(redOrangeRatio / 0.35) +
            0.20 * clamp01(whiteCoreRatio / 0.12) +
            0.15 * clamp01(vStd / 50.0) +
            0.15 * dynamicScore;
        finalScore -= skinPenalty + yellowPenalty + (reflectionLike ? 0.12 : 0.0) + (fingerLike ? 0.10 : 0.0);
        if (smallRegion) finalScore += 0.02;
        finalScore = clamp01(finalScore);
        if (finalScore < (smallRegion ? 0.27 : 0.35)) continue;

        if (dynamicScore > 0.22) hasFlicker = true;
        totalFireArea += contourAreaValue * scaleX * scaleY;

        Rect originalBox(cvRound(box.x * scaleX), cvRound(box.y * scaleY),
            cvRound(box.width * scaleX), cvRound(box.height * scaleY));
        const int padX = max(4, cvRound(originalBox.width * 0.12));
        const int padY = max(4, cvRound(originalBox.height * 0.12));
        originalBox = Rect(originalBox.x - padX, originalBox.y - padY,
            originalBox.width + padX * 2, originalBox.height + padY * 2) &
            Rect(0, 0, inputFrame.cols, inputFrame.rows);
        if (originalBox.empty()) continue;

        char label[64];
        snprintf(label, sizeof(label), "FIRE SCORE %.2f", finalScore);

        DetectionBox detection;
        detection.box = originalBox;
        detection.label = label;
        detection.score = finalScore;
        detection.strongFireEvidence = strongFireEvidence;
        detection.tinyCandidate = tinyCandidate;
        detection.skinLikeCandidate = connectedSkin;
        detection.coreHaloEvidence = coreHaloEvidence;
        detection.reflectionLikeCandidate = reflectionLike;
        detection.brightBackgroundEvidence = brightBackground;
        detection.fingerLikeCandidate = fingerLike;
        detection.skinSeparatedFlameEvidence = skinSeparatedFlame;
        detection.requiresExtendedConfirmation = fingerLike ||
            (!coreHaloEvidence && (reflectionLike || connectedSkin || yellowDominantRatio >= 0.40));
        detection.trackedPersistenceEvidence = stableTrackedFire;
        detection.brightnessDiffMean = brightnessDiffMean;
        detection.maskChangeRatio = maskChangeRatio;
        detection.redOrangeRatio = redOrangeRatio;
        acceptedBoxes.push_back(std::move(detection));
    }

    const FireTrackingResult tracking = candidateTracker_.update(acceptedBoxes);
    result.candidate = tracking.candidate;
    result.detected = tracking.detected;
    result.candidateDisplayReady = tracking.candidateDisplayReady;
    result.flicker = hasFlicker;
    result.area = totalFireArea;
    result.hitCount = tracking.hitCount;
    result.confirmCount = tracking.confirmCount;
    result.boxes = tracking.boxes;

#if FIRE_DEBUG_VIEW
    if (frameIndex % max(1, FIRE_DEBUG_SAMPLE_INTERVAL) == 0)
    {
        const Size preview(FIRE_DEBUG_TILE_WIDTH, FIRE_DEBUG_TILE_HEIGHT);
        resize(fireColorMask, result.debugImages.fireColorMask, preview, 0, 0, INTER_NEAREST);
        resize(skinMask, result.debugImages.skinMask, preview, 0, 0, INTER_NEAREST);
        resize(foregroundMask, result.debugImages.foregroundMask, preview, 0, 0, INTER_NEAREST);
        resize(candidateMask, result.debugImages.candidateMask, preview, 0, 0, INTER_NEAREST);
    }
#endif

    prevVal = val.clone();
    prevFireMask = fireColorMask.clone();
    ++frameIndex;
    return result;
}