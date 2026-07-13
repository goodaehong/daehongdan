#include "FireDetector_1.h"
#include "AppConfig.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace cv;
using namespace std;

namespace
{
    const int DETECT_WIDTH = 960;
    const int DETECT_HEIGHT = 540;

    const int FIRE_CONFIRM_FRAMES = 3;
    // 라이터처럼 작은 화염은 한두 프레임 후보가 끊길 수 있다.
    // 최대 2회의 미검출은 같은 화염의 일시적 누락으로 허용한다.
    const int MAX_CANDIDATE_MISSES = 2;

    // 화재 확정 전에는 한 번의 순간 미검출을 허용한다.
    // 실제 불꽃이 한 프레임씩 끊겨도 누적 상태가 전부 초기화되지 않게 한다.
    const int MAX_PRECONFIRM_MISSES = 1;

    // 민감도를 소폭 높이되, 반사광 후보에는 별도의 더 강한 확정 조건을 적용한다.
    const double NEW_TRACK_MIN_SCORE = 0.42;
    const double STRONG_FIRE_SCORE = 0.65;
    const double KEEP_FIRE_SCORE = 0.59;
    const int STRONG_CONFIRM_FRAMES = 2;
    const int MAX_WEAK_KEEP_FRAMES = 2;

    double clamp01(double v)
    {
        if (v < 0.0) return 0.0;
        if (v > 1.0) return 1.0;
        return v;
    }

    double safeRatio(double a, double b)
    {
        if (b <= 0.0)
            return 0.0;

        return a / b;
    }

    double rectAreaDouble(const Rect& r)
    {
        return static_cast<double>(r.width) *
            static_cast<double>(r.height);
    }
}

FireDetector::FireDetector()
{
    mog2 = createBackgroundSubtractorMOG2(
        200,
        16.0,
        false
    );

    mog2->setDetectShadows(false);
}

void FireDetector::reset()
{
    prevGray.release();
    prevVal.release();
    prevFireMask.release();

    mog2 = createBackgroundSubtractorMOG2(
        200,
        16.0,
        false
    );

    mog2->setDetectShadows(false);

    frameIndex = 0;
    fireConfirmCount = 0;
    candidateMissCount = 0;
    strongFireCount = 0;
    weakKeepCount = 0;
    fireConfirmed = false;
    previousCandidateBox = Rect();
}


double FireDetector::calculateIoU(
    const Rect& a,
    const Rect& b
) const
{
    if (a.empty() || b.empty())
        return 0.0;

    const Rect intersection = a & b;

    if (intersection.empty())
        return 0.0;

    const double intersectionArea =
        static_cast<double>(intersection.area());

    const double unionArea =
        static_cast<double>(a.area()) +
        static_cast<double>(b.area()) -
        intersectionArea;

    if (unionArea <= 0.0)
        return 0.0;

    return intersectionArea / unionArea;
}

bool FireDetector::isSameCandidate(
    const Rect& previous,
    const Rect& current
) const
{
    if (previous.empty() || current.empty())
        return false;

    const double iou =
        calculateIoU(previous, current);

    const Point2d previousCenter(
        previous.x + previous.width * 0.5,
        previous.y + previous.height * 0.5
    );

    const Point2d currentCenter(
        current.x + current.width * 0.5,
        current.y + current.height * 0.5
    );

    const double centerDistance =
        norm(previousCenter - currentCenter);

    const double referenceSize =
        static_cast<double>(
            max(
                max(previous.width, previous.height),
                max(current.width, current.height)
            )
            );

    // 작은 화염은 박스 모양이 흔들려 IoU가 낮아질 수 있으므로
    // 중심점 거리 조건을 함께 사용한다.
    return
        iou >= 0.15 ||
        centerDistance <= max(20.0, referenceSize * 0.75);
}

void FireDetector::cleanupBinaryMask(
    Mat& mask,
    int openSize,
    int closeSize,
    int dilateSize
) const
{
    if (mask.empty())
        return;

    if (openSize > 1)
    {
        Mat openKernel = getStructuringElement(
            MORPH_ELLIPSE,
            Size(openSize, openSize)
        );

        morphologyEx(
            mask,
            mask,
            MORPH_OPEN,
            openKernel,
            Point(-1, -1),
            1
        );
    }

    if (closeSize > 1)
    {
        Mat closeKernel = getStructuringElement(
            MORPH_ELLIPSE,
            Size(closeSize, closeSize)
        );

        morphologyEx(
            mask,
            mask,
            MORPH_CLOSE,
            closeKernel,
            Point(-1, -1),
            1
        );
    }

    if (dilateSize > 1)
    {
        Mat dilateKernel = getStructuringElement(
            MORPH_ELLIPSE,
            Size(dilateSize, dilateSize)
        );

        dilate(
            mask,
            mask,
            dilateKernel,
            Point(-1, -1),
            1
        );
    }
}

Mat FireDetector::makeSkinMask(
    const Mat& frame,
    const Mat& hsv
) const
{
    Mat ycrcb;

    cvtColor(
        frame,
        ycrcb,
        COLOR_BGR2YCrCb
    );

    Mat skinYCrCb;

    inRange(
        ycrcb,
        Scalar(0, 133, 77),
        Scalar(255, 180, 135),
        skinYCrCb
    );

    Mat skinHSV;

    inRange(
        hsv,
        Scalar(0, 20, 50),
        Scalar(25, 230, 255),
        skinHSV
    );

    Mat skinMask;

    bitwise_and(
        skinYCrCb,
        skinHSV,
        skinMask
    );

    Mat closeKernel = getStructuringElement(
        MORPH_ELLIPSE,
        Size(5, 5)
    );

    morphologyEx(
        skinMask,
        skinMask,
        MORPH_CLOSE,
        closeKernel,
        Point(-1, -1),
        1
    );

    Mat dilateKernel = getStructuringElement(
        MORPH_ELLIPSE,
        Size(5, 5)
    );

    dilate(
        skinMask,
        skinMask,
        dilateKernel,
        Point(-1, -1),
        1
    );

    return skinMask;
}

Mat FireDetector::makeFireColorMask(
    const Mat& frame,
    const Mat& hsv
) const
{
    vector<Mat> hsvCh;

    split(
        hsv,
        hsvCh
    );

    Mat h = hsvCh[0];
    Mat s = hsvCh[1];
    Mat v = hsvCh[2];

    vector<Mat> bgrCh;

    split(
        frame,
        bgrCh
    );

    Mat b = bgrCh[0];
    Mat g = bgrCh[1];
    Mat r = bgrCh[2];

    // ==================================================
    // 1. HSV 화염 후보
    // 노란색 단독 영역은 초기 후보에서 제외한다.
    // ==================================================
    Mat hueRed1;
    Mat hueRed2;
    Mat hueOrange;

    inRange(
        h,
        Scalar(0),
        Scalar(15),
        hueRed1
    );

    inRange(
        h,
        Scalar(170),
        Scalar(179),
        hueRed2
    );

    inRange(
        h,
        Scalar(16),
        Scalar(35),
        hueOrange
    );

    Mat hueFire;

    bitwise_or(
        hueRed1,
        hueRed2,
        hueFire
    );

    bitwise_or(
        hueFire,
        hueOrange,
        hueFire
    );

    Mat satOk;
    Mat valOk;

    compare(
        s,
        55,
        satOk,
        CMP_GT
    );

    compare(
        v,
        110,
        valOk,
        CMP_GT
    );

    Mat hsvFire;

    bitwise_and(
        hueFire,
        satOk,
        hsvFire
    );

    bitwise_and(
        hsvFire,
        valOk,
        hsvFire
    );

    // ==================================================
    // 2. BGR 화염 후보
    // 노란색/피부톤 제거를 위해 R-G 차이를 강하게 본다.
    // ==================================================
    Mat rBright;
    Mat gBright;

    compare(
        r,
        125,
        rBright,
        CMP_GT
    );

    compare(
        g,
        45,
        gBright,
        CMP_GT
    );

    Mat rbDiff;
    Mat gbDiff;
    Mat rgDiff;

    subtract(
        r,
        b,
        rbDiff
    );

    subtract(
        g,
        b,
        gbDiff
    );

    subtract(
        r,
        g,
        rgDiff
    );

    Mat rbOk;
    Mat gbOk;
    Mat rgOk;

    compare(
        rbDiff,
        40,
        rbOk,
        CMP_GT
    );

    compare(
        gbDiff,
        8,
        gbOk,
        CMP_GT
    );

    compare(
        rgDiff,
        5,
        rgOk,
        CMP_GT
    );

    Mat bgrFire;

    bitwise_and(
        rBright,
        gBright,
        bgrFire
    );

    bitwise_and(
        bgrFire,
        rbOk,
        bgrFire
    );

    bitwise_and(
        bgrFire,
        gbOk,
        bgrFire
    );

    bitwise_and(
        bgrFire,
        rgOk,
        bgrFire
    );

    Mat chromaticFire;

    bitwise_and(
        hsvFire,
        bgrFire,
        chromaticFire
    );

    // ==================================================
    // 3. 흰색/밝은 화염 중심부
    // 단독 흰색 반사광은 제외하고, 주변에 주황/빨강 화염색이 있을 때만 인정한다.
    // ==================================================
    Mat whiteCoreSat;
    Mat whiteCoreVal;
    Mat whiteCore;

    compare(
        s,
        65,
        whiteCoreSat,
        CMP_LT
    );

    compare(
        v,
        225,
        whiteCoreVal,
        CMP_GT
    );

    bitwise_and(
        whiteCoreSat,
        whiteCoreVal,
        whiteCore
    );

    Mat fireHalo;

    Mat haloKernel = getStructuringElement(
        MORPH_ELLIPSE,
        Size(9, 9)
    );

    dilate(
        chromaticFire,
        fireHalo,
        haloKernel,
        Point(-1, -1),
        1
    );

    Mat whiteCoreNearFire;

    bitwise_and(
        whiteCore,
        fireHalo,
        whiteCoreNearFire
    );

    Mat fireColorMask;

    bitwise_or(
        chromaticFire,
        whiteCoreNearFire,
        fireColorMask
    );

    cleanupBinaryMask(
        fireColorMask,
        3,
        3,
        1
    );

    return fireColorMask;
}

Mat FireDetector::makeForegroundMask(
    const Mat& frame,
    const Mat& gray
)
{
    Mat foreground =
        Mat::zeros(
            gray.size(),
            CV_8UC1
        );

    if (!mog2.empty())
    {
        Mat mogMask;

        mog2->apply(
            frame,
            mogMask,
            0.006
        );

        threshold(
            mogMask,
            mogMask,
            220,
            255,
            THRESH_BINARY
        );

        if (frameIndex >= 8)
        {
            bitwise_or(
                foreground,
                mogMask,
                foreground
            );
        }
    }

    if (!prevGray.empty() &&
        prevGray.size() == gray.size())
    {
        Mat diff;

        absdiff(
            prevGray,
            gray,
            diff
        );

        Mat diffMask;

        threshold(
            diff,
            diffMask,
            10,
            255,
            THRESH_BINARY
        );

        bitwise_or(
            foreground,
            diffMask,
            foreground
        );
    }

    cleanupBinaryMask(
        foreground,
        3,
        3,
        3
    );

    return foreground;
}

DetectionResult FireDetector::detect(const Mat& inputFrame)
{
    DetectionResult result;

    if (inputFrame.empty())
        return result;

    // ==================================================
    // 1. 전처리
    // ==================================================
    Mat frame;

    resize(
        inputFrame,
        frame,
        Size(DETECT_WIDTH, DETECT_HEIGHT),
        0,
        0,
        INTER_LINEAR
    );

    const double scaleX =
        static_cast<double>(inputFrame.cols) /
        static_cast<double>(frame.cols);

    const double scaleY =
        static_cast<double>(inputFrame.rows) /
        static_cast<double>(frame.rows);

    Mat gray;

    cvtColor(
        frame,
        gray,
        COLOR_BGR2GRAY
    );

    GaussianBlur(
        gray,
        gray,
        Size(3, 3),
        0.0
    );

    Mat hsv;

    cvtColor(
        frame,
        hsv,
        COLOR_BGR2HSV
    );

    vector<Mat> hsvCh;

    split(
        hsv,
        hsvCh
    );

    Mat hue = hsvCh[0];
    Mat sat = hsvCh[1];
    Mat val = hsvCh[2];

    // ==================================================
    // 2. 피부 / 화염색 / 전경 마스크
    // ==================================================
    Mat skinMask =
        makeSkinMask(
            frame,
            hsv
        );

    Mat fireColorMask =
        makeFireColorMask(
            frame,
            hsv
        );

    // 피부색 영역은 화염색 마스크에서 먼저 제거한다.
   /* Mat notSkinMask;

    bitwise_not(
        skinMask,
        notSkinMask
    );

    bitwise_and(
        fireColorMask,
        notSkinMask,
        fireColorMask
    );*/

    Mat foregroundMask =
        makeForegroundMask(
            frame,
            gray
        );

    Mat rawCandidateMask;

    bitwise_and(
        fireColorMask,
        foregroundMask,
        rawCandidateMask
    );

    Mat candidateMask =
        rawCandidateMask.clone();

    // 후보 영역을 너무 키우지 않는다.
    cleanupBinaryMask(
        candidateMask,
        3,
        3,
        3
    );

    // ==================================================
    // 3. Optical Flow
    // 전체 화면 Farneback Optical Flow는 처리 시간이 너무 커서
    // 비활성화한다. 밝기 변화와 마스크 변화율만으로 동적 특성을
    // 판정한다. 나중에 필요하면 후보 ROI에만 적용한다.
    // ==================================================
    Mat flowX;
    Mat flowY;

    // 현재 버전에서는 Optical Flow를 계산하지 않는다.
    // Flow가 실제로 채워진 경우에만 아래 Flow 기반 제거 조건을 적용한다.
    const bool hasFlowData =
        !flowX.empty() &&
        !flowY.empty();

    // ==================================================
    // 4. contour 추출
    // ==================================================
    vector<vector<Point>> contours;

    Mat contourMask =
        candidateMask.clone();

    findContours(
        contourMask,
        contours,
        RETR_EXTERNAL,
        CHAIN_APPROX_SIMPLE
    );

    sort(
        contours.begin(),
        contours.end(),
        [](const vector<Point>& a, const vector<Point>& b)
        {
            return contourArea(a) > contourArea(b);
        }
    );

    const size_t MAX_CONTOURS_TO_CHECK = 20;
    const double MIN_CONTOUR_AREA = 10.0;

    const size_t contourLimit =
        min(
            contours.size(),
            MAX_CONTOURS_TO_CHECK
        );

    bool hasFlickerLikeMotion = false;

    double totalFireArea = 0.0;

    vector<DetectionBox> acceptedBoxes;

    // ==================================================
    // 5. 후보 영역 분석
    // ==================================================
    for (size_t i = 0; i < contourLimit; ++i)
    {
        const double contourAreaValue =
            contourArea(
                contours[i]
            );

        if (contourAreaValue < MIN_CONTOUR_AREA)
            continue;

        Rect box =
            boundingRect(
                contours[i]
            );

        if (box.width < 3 ||
            box.height < 3)
        {
            continue;
        }

        box &=
            Rect(
                0,
                0,
                frame.cols,
                frame.rows
            );

        if (box.empty())
            continue;

        const double boxArea =
            rectAreaDouble(
                box
            );

        if (boxArea <= 0.0)
            continue;

        const bool smallRegion =
            boxArea < 900.0;

        const int shortSide =
            max(
                1,
                min(box.width, box.height)
            );

        const int longSide =
            max(
                box.width,
                box.height
            );

        const double aspect =
            static_cast<double>(longSide) /
            static_cast<double>(shortSide);

        if (aspect > 5.5)
            continue;


        vector<Point> hullPoints;
        convexHull(contours[i], hullPoints);

        const double hullArea =
            hullPoints.size() >= 3
            ? contourArea(hullPoints)
            : 0.0;

        const double solidity =
            safeRatio(
                contourAreaValue,
                hullArea
            );

        // ==================================================
        // 현재 contour 자체만 분석하기 위한 로컬 마스크
        // boundingRect 전체를 분석하면 옆의 실제 불꽃 픽셀이
        // 손가락/반사광 후보의 점수에 섞일 수 있다.
        // ==================================================
        Mat componentMask =
            Mat::zeros(
                box.size(),
                CV_8UC1
            );

        vector<Point> localContour;
        localContour.reserve(contours[i].size());

        for (const Point& point : contours[i])
        {
            localContour.push_back(
                Point(
                    point.x - box.x,
                    point.y - box.y
                )
            );
        }

        vector<vector<Point>> localContours;
        localContours.push_back(localContour);

        drawContours(
            componentMask,
            localContours,
            0,
            Scalar(255),
            FILLED
        );

        const int componentPixelCount =
            countNonZero(componentMask);

        if (componentPixelCount <= 0)
            continue;

        Mat roiFire =
            fireColorMask(box);

        Mat roiForeground =
            foregroundMask(box);

        Mat roiCandidate =
            rawCandidateMask(box);

        Mat roiH =
            hue(box);

        Mat roiS =
            sat(box);

        Mat roiV =
            val(box);

        Mat roiSkin =
            skinMask(box);

        Mat componentFire;
        Mat componentForeground;
        Mat componentCandidate;
        Mat componentSkin;

        bitwise_and(
            roiFire,
            componentMask,
            componentFire
        );

        bitwise_and(
            roiForeground,
            componentMask,
            componentForeground
        );

        bitwise_and(
            roiCandidate,
            componentMask,
            componentCandidate
        );

        bitwise_and(
            roiSkin,
            componentMask,
            componentSkin
        );

        const int firePixelCount =
            countNonZero(componentFire);

        const int foregroundPixelCount =
            countNonZero(componentForeground);

        const int candidatePixelCount =
            countNonZero(componentCandidate);

        if (firePixelCount <= 0 ||
            foregroundPixelCount <= 0 ||
            candidatePixelCount <= 0)
        {
            continue;
        }

        const double colorRatio =
            safeRatio(
                static_cast<double>(firePixelCount),
                boxArea
            );

        const double foregroundRatio =
            safeRatio(
                static_cast<double>(foregroundPixelCount),
                boxArea
            );

        const double candidateRatio =
            safeRatio(
                static_cast<double>(candidatePixelCount),
                boxArea
            );

        if (smallRegion)
        {
            if (colorRatio < 0.015)
                continue;

            if (candidateRatio < 0.004)
                continue;
        }
        else
        {
            if (colorRatio < 0.035)
                continue;

            if (candidateRatio < 0.010)
                continue;

            if (foregroundRatio < 0.010)
                continue;
        }

        // ==================================================
        // 6. 색상 세부 분석
        // ==================================================
        Mat redHue1;
        Mat redHue2;
        Mat redHue;
        Mat orangeHue;
        Mat yellowHue;
        Mat yellowOrangeHue;

        inRange(
            roiH,
            Scalar(0),
            Scalar(15),
            redHue1
        );

        inRange(
            roiH,
            Scalar(170),
            Scalar(179),
            redHue2
        );

        bitwise_or(
            redHue1,
            redHue2,
            redHue
        );

        inRange(
            roiH,
            Scalar(16),
            Scalar(28),
            orangeHue
        );

        inRange(
            roiH,
            Scalar(29),
            Scalar(45),
            yellowHue
        );

        // 카메라에서는 노란 물체가 H=20~28 부근으로 들어오는 경우가 많다.
        // 기존 yellowHue(29~45)만 보면 이 영역이 orangeHue로 분류되어
        // redOrangeRatio를 크게 올리는 문제가 있으므로 별도로 측정한다.
        inRange(
            roiH,
            Scalar(20),
            Scalar(45),
            yellowOrangeHue
        );

        Mat satStrong;
        Mat valBright;

        compare(
            roiS,
            65,
            satStrong,
            CMP_GT
        );

        compare(
            roiV,
            120,
            valBright,
            CMP_GT
        );

        Mat redOrangeFire;

        bitwise_or(
            redHue,
            orangeHue,
            redOrangeFire
        );

        bitwise_and(
            redOrangeFire,
            satStrong,
            redOrangeFire
        );

        bitwise_and(
            redOrangeFire,
            valBright,
            redOrangeFire
        );

        bitwise_and(
            redOrangeFire,
            componentFire,
            redOrangeFire
        );

        // 노란/주황 물체와 구분하기 위한 순수 적색 계열 비율
        Mat pureRedFire;

        bitwise_and(
            redHue,
            satStrong,
            pureRedFire
        );

        bitwise_and(
            pureRedFire,
            valBright,
            pureRedFire
        );

        bitwise_and(
            pureRedFire,
            componentFire,
            pureRedFire
        );

        Mat yellowObject;

        bitwise_and(
            yellowHue,
            satStrong,
            yellowObject
        );

        bitwise_and(
            yellowObject,
            valBright,
            yellowObject
        );

        bitwise_and(
            yellowObject,
            componentForeground,
            yellowObject
        );

        Mat yellowOrangeObject;

        bitwise_and(
            yellowOrangeHue,
            satStrong,
            yellowOrangeObject
        );

        bitwise_and(
            yellowOrangeObject,
            valBright,
            yellowOrangeObject
        );

        bitwise_and(
            yellowOrangeObject,
            componentForeground,
            yellowOrangeObject
        );

        Mat whiteCoreSat;
        Mat whiteCoreVal;
        Mat whiteCoreFire;

        compare(
            roiS,
            65,
            whiteCoreSat,
            CMP_LT
        );

        compare(
            roiV,
            220,
            whiteCoreVal,
            CMP_GT
        );

        bitwise_and(
            whiteCoreSat,
            whiteCoreVal,
            whiteCoreFire
        );

        bitwise_and(
            whiteCoreFire,
            componentFire,
            whiteCoreFire
        );

        const int redOrangePixelCount =
            countNonZero(redOrangeFire);

        const int pureRedPixelCount =
            countNonZero(pureRedFire);

        const int whiteCorePixelCount =
            countNonZero(whiteCoreFire);

        // 흰색 중심부가 실제로 주황/빨강 화염층에 둘러싸였는지 확인한다.
        // 손가락의 작은 반사광은 밝은 점이 생겨도 주변에 화염색 고리가
        // 충분히 형성되지 않는 경우가 많다.
        Mat whiteCoreDilated;
        Mat whiteCoreRing;
        Mat whiteCoreInverse;
        Mat haloRedOrange;

        Mat coreHaloKernel = getStructuringElement(
            MORPH_ELLIPSE,
            Size(9, 9)
        );

        dilate(
            whiteCoreFire,
            whiteCoreDilated,
            coreHaloKernel,
            Point(-1, -1),
            1
        );

        bitwise_not(
            whiteCoreFire,
            whiteCoreInverse
        );

        bitwise_and(
            whiteCoreDilated,
            whiteCoreInverse,
            whiteCoreRing
        );

        bitwise_and(
            whiteCoreRing,
            componentMask,
            whiteCoreRing
        );

        bitwise_and(
            whiteCoreRing,
            redOrangeFire,
            haloRedOrange
        );

        const int haloRingPixelCount =
            countNonZero(whiteCoreRing);

        const int haloRedOrangePixelCount =
            countNonZero(haloRedOrange);

        // 손가락 반사광도 피부색 주위에 흰 중심과 주황 테두리가 생겨
        // 기존 coreHaloEvidence를 통과할 수 있다. 따라서 화염 고리 픽셀이
        // 피부 마스크 안쪽인지, 피부 밖의 독립적인 화염색인지 구분한다.
        Mat haloRedOrangeSkin;
        Mat notComponentSkin;
        Mat haloRedOrangeNonSkin;

        bitwise_and(
            haloRedOrange,
            componentSkin,
            haloRedOrangeSkin
        );

        bitwise_not(
            componentSkin,
            notComponentSkin
        );

        bitwise_and(
            haloRedOrange,
            notComponentSkin,
            haloRedOrangeNonSkin
        );

        const int haloRedOrangeSkinPixelCount =
            countNonZero(haloRedOrangeSkin);

        const int haloRedOrangeNonSkinPixelCount =
            countNonZero(haloRedOrangeNonSkin);

        const double haloSupportRatio =
            safeRatio(
                static_cast<double>(haloRedOrangePixelCount),
                static_cast<double>(haloRingPixelCount)
            );

        const double haloSkinRatio =
            safeRatio(
                static_cast<double>(haloRedOrangeSkinPixelCount),
                static_cast<double>(haloRedOrangePixelCount)
            );

        const double nonSkinHaloRatio =
            safeRatio(
                static_cast<double>(haloRedOrangeNonSkinPixelCount),
                static_cast<double>(haloRedOrangePixelCount)
            );

        // 피부 후보에서 고리 전체가 피부색이면 손가락 반사광일 가능성이 높다.
        // 피부 후보일 때는 최소한 일부 고리가 피부 밖 화염색으로 분리돼야 한다.
        const bool skinSeparatedHaloEvidence =
            haloRedOrangeNonSkinPixelCount >= 4 &&
            nonSkinHaloRatio >= 0.30;

        const bool rawCoreHaloEvidence =
            whiteCorePixelCount >= 3 &&
            haloRedOrangePixelCount >= 6 &&
            haloSupportRatio >= 0.16;

        // 피부 마스크는 따뜻한 색의 실제 화염까지 피부로 잡는 경우가 있다.
        // 따라서 core-halo 자체를 피부 마스크 때문에 무효화하지 않는다.
        // 피부 연결성은 아래 fingerLikeCandidate 판정에서 보조 조건으로만 사용한다.
        const bool coreHaloEvidence =
            rawCoreHaloEvidence;

        const double redOrangeRatio =
            safeRatio(
                static_cast<double>(redOrangePixelCount),
                static_cast<double>(firePixelCount)
            );

        const double pureRedRatio =
            safeRatio(
                static_cast<double>(pureRedPixelCount),
                static_cast<double>(firePixelCount)
            );

        const double yellowObjectRatio =
            safeRatio(
                static_cast<double>(countNonZero(yellowObject)),
                static_cast<double>(foregroundPixelCount)
            );

        const double yellowDominantRatio =
            safeRatio(
                static_cast<double>(countNonZero(yellowOrangeObject)),
                static_cast<double>(foregroundPixelCount)
            );

        const double whiteCoreRatio =
            safeRatio(
                static_cast<double>(whiteCorePixelCount),
                static_cast<double>(firePixelCount)
            );

        Scalar meanV;
        Scalar stdV;

        meanStdDev(
            roiV,
            meanV,
            stdV,
            componentFire
        );

        const double vStd =
            stdV[0];

        Scalar meanS;
        Scalar stdS;

        meanStdDev(
            roiS,
            meanS,
            stdS,
            componentFire
        );

        const double sStd =
            stdS[0];

        // ==================================================
        // 7. 피부/사람 오검출 제거
        // ==================================================
        // contour 내부 전체를 분모로 사용하면 후보 사이의 빈 영역이나
        // 주변 피부까지 함께 계산되어 실제 화염도 피부 비율이 과도하게
        // 높아질 수 있다. 실제 화염 후보 픽셀과 피부 마스크가 겹치는
        // 픽셀만 계산한다.
        Mat candidateSkinOverlap;

        bitwise_and(
            componentSkin,
            componentCandidate,
            candidateSkinOverlap
        );

        const int candidateSkinPixelCount =
            countNonZero(candidateSkinOverlap);

        const double candidateSkinRatio =
            safeRatio(
                static_cast<double>(candidateSkinPixelCount),
                static_cast<double>(candidatePixelCount)
            );

        // 후보 박스 바로 주변이 피부 마스크로 이어지는지 확인한다.
        // 손가락 끝의 반사광은 후보 자체뿐 아니라 주변도 피부로 연결되지만,
        // 라이터 불꽃은 일반적으로 피부 영역과 공간적으로 분리된다.
        const int skinContextPadX =
            max(6, box.width / 2);

        const int skinContextPadY =
            max(6, box.height / 2);

        Rect skinContextBox(
            max(0, box.x - skinContextPadX),
            max(0, box.y - skinContextPadY),
            min(frame.cols, box.x + box.width + skinContextPadX) -
            max(0, box.x - skinContextPadX),
            min(frame.rows, box.y + box.height + skinContextPadY) -
            max(0, box.y - skinContextPadY)
        );

        Mat skinContextRing =
            Mat::ones(
                skinContextBox.size(),
                CV_8UC1
            ) * 255;

        Rect localCandidateBox(
            box.x - skinContextBox.x,
            box.y - skinContextBox.y,
            box.width,
            box.height
        );

        localCandidateBox &=
            Rect(
                0,
                0,
                skinContextRing.cols,
                skinContextRing.rows
            );

        if (!localCandidateBox.empty())
        {
            skinContextRing(localCandidateBox).setTo(0);
        }

        Mat surroundingSkin;

        bitwise_and(
            skinMask(skinContextBox),
            skinContextRing,
            surroundingSkin
        );

        const double surroundingSkinRatio =
            safeRatio(
                static_cast<double>(countNonZero(surroundingSkin)),
                static_cast<double>(countNonZero(skinContextRing))
            );

        // 작은 후보는 1~2개의 압축 노이즈 픽셀만으로도 비율이 크게 나올 수 있다.
        // 따라서 비율뿐 아니라 절대 픽셀 수도 함께 요구한다.
        const bool reliableWhiteCore =
            whiteCorePixelCount >= (smallRegion ? 3 : 6) &&
            redOrangePixelCount >= (smallRegion ? 8 : 16) &&
            whiteCoreRatio >= (smallRegion ? 0.020 : 0.030);

        const bool reliablePureRed =
            pureRedPixelCount >= (smallRegion ? 4 : 8) &&
            redOrangePixelCount >= (smallRegion ? 8 : 16) &&
            pureRedRatio >= (smallRegion ? 0.070 : 0.100);

        const bool hasStrongFlameCore =
            reliableWhiteCore || reliablePureRed;

        // 피부가 후보 대부분을 차지하고 화염 중심 증거가 약하면 제거한다.
        // 피부색만 보고 바로 제거하지 않아 실제 라이터 불꽃 손실은 줄인다.
        if (candidateSkinRatio > 0.60 &&
            !hasStrongFlameCore &&
            redOrangeRatio < 0.35)
        {
            continue;
        }

        double skinPenalty = 0.0;

        if (candidateSkinRatio > 0.50 &&
            !hasStrongFlameCore)
        {
            skinPenalty = 0.12;
        }
        else if (candidateSkinRatio > 0.35 &&
            whiteCoreRatio < 0.015 &&
            pureRedRatio < 0.050)
        {
            skinPenalty = 0.06;
        }

        // ==================================================
        // 8. 화염 핵심 색 조건
        // 노란색만으로는 통과 불가.
        // ==================================================
        const bool redEnough =
            redOrangeRatio >
            (
                smallRegion ? 0.04 : 0.08
            );

        const bool whiteCoreEnough =
            whiteCoreRatio >
            (
                smallRegion ? 0.010 : 0.020
            );

        if (!redEnough &&
            !whiteCoreEnough)
        {
            continue;
        }

        // ==================================================
        // 9. 노란색 물체 제거
        // ==================================================
        // 움직이는 노란 물체가 H=20~28 영역에서 orange로 잡히는 경우까지 본다.
        // 단순히 노란색 비율만 높다고 제거하면 실제 노란 화염도 놓칠 수 있으므로
        // 색상이 균일하고, 순수 적색/흰 중심이 약한 단단한 물체에만 강하게 적용한다.
        const bool yellowDominant =
            yellowDominantRatio > (smallRegion ? 0.58 : 0.48);

        const bool weakFlameColorLayer =
            pureRedRatio < (smallRegion ? 0.045 : 0.060) &&
            whiteCoreRatio < (smallRegion ? 0.018 : 0.025);

        const bool uniformRigidYellow =
            solidity > 0.78 &&
            vStd < 32.0 &&
            sStd < 38.0;

        if (yellowDominant &&
            weakFlameColorLayer &&
            uniformRigidYellow &&
            boxArea >= 220.0)
        {
            continue;
        }

        if (yellowObjectRatio > 0.35 &&
            redOrangeRatio < 0.22 &&
            whiteCoreRatio < 0.080)
        {
            continue;
        }

        if (!smallRegion &&
            yellowObjectRatio > 0.25 &&
            redOrangeRatio < 0.28 &&
            whiteCoreRatio < 0.100)
        {
            continue;
        }

        if (!smallRegion &&
            yellowObjectRatio > 0.18 &&
            vStd < 35.0 &&
            whiteCoreRatio < 0.100)
        {
            continue;
        }

        // ==================================================
        // 10. 화염 동적 특성 분석
        // ==================================================
        double brightnessDiffMean = 0.0;
        double maskChangeRatio = 0.0;

        if (!prevVal.empty() &&
            !prevFireMask.empty() &&
            prevVal.size() == val.size() &&
            prevFireMask.size() == fireColorMask.size())
        {
            Mat prevRoiV =
                prevVal(box);

            Mat vDiff;

            absdiff(
                roiV,
                prevRoiV,
                vDiff
            );

            brightnessDiffMean =
                mean(
                    vDiff,
                    componentFire
                )[0];

            Mat prevRoiFire =
                prevFireMask(box);

            Mat previousComponentFire;

            bitwise_and(
                prevRoiFire,
                componentMask,
                previousComponentFire
            );

            Mat maskXor;

            bitwise_xor(
                componentFire,
                previousComponentFire,
                maskXor
            );

            const int maskChangeCount =
                countNonZero(maskXor);

            const int prevFireCount =
                countNonZero(previousComponentFire);

            maskChangeRatio =
                safeRatio(
                    static_cast<double>(maskChangeCount),
                    static_cast<double>(
                        max(
                            1,
                            firePixelCount + prevFireCount
                        )
                        )
                );
        }

        const double flickerScore =
            0.5 * clamp01(brightnessDiffMean / 18.0) +
            0.5 * clamp01(maskChangeRatio / 0.18);

        // ==================================================
        // 11. Optical Flow 기반 흐름 분석
        // ==================================================
        double meanMag = 0.0;
        double flowDisorder = 0.0;

        if (hasFlowData &&
            flowX.size() == gray.size() &&
            flowY.size() == gray.size())
        {
            Mat roiFlowX =
                flowX(box);

            Mat roiFlowY =
                flowY(box);

            Mat flowMask =
                componentCandidate;

            if (countNonZero(flowMask) <= 0)
            {
                flowMask =
                    componentFire;
            }

            Scalar meanFx =
                mean(
                    roiFlowX,
                    flowMask
                );

            Scalar meanFy =
                mean(
                    roiFlowY,
                    flowMask
                );

            Mat mag;
            Mat angle;

            cartToPolar(
                roiFlowX,
                roiFlowY,
                mag,
                angle,
                false
            );

            meanMag =
                mean(
                    mag,
                    flowMask
                )[0];

            const double meanVectorMag =
                sqrt(
                    meanFx[0] * meanFx[0] +
                    meanFy[0] * meanFy[0]
                );

            if (meanMag > 0.0001)
            {
                flowDisorder =
                    1.0 -
                    clamp01(
                        meanVectorMag / meanMag
                    );
            }
        }

        const double flowScore =
            clamp01(meanMag / 1.4) *
            (
                0.35 +
                0.65 * flowDisorder
                );

        const double dynamicScore =
            max(
                flickerScore,
                flowScore
            );

        const bool dynamicEnough =
            smallRegion ?
            (
                dynamicScore > 0.07 ||
                brightnessDiffMean > 1.7 ||
                maskChangeRatio > 0.020
                ) :
            (
                dynamicScore > 0.15 ||
                brightnessDiffMean > 3.5 ||
                maskChangeRatio > 0.050
                );

        if (frameIndex > 3 &&
            !dynamicEnough)
        {
            continue;
        }

        if (hasFlowData &&
            !smallRegion &&
            flowDisorder < 0.20 &&
            flickerScore < 0.25)
        {
            continue;
        }

        if (!smallRegion &&
            vStd < 18.0 &&
            sStd < 20.0 &&
            whiteCoreRatio < 0.080)
        {
            continue;
        }

        // 점수만 높다고 화재로 확정하지 않는다.
        // 실제 화염은 보통 순수 적색/흰 중심의 색상 층이 있거나,
        // 노란 단색 물체보다 밝기·채도·형태 변화가 복합적으로 나타난다.
        const bool hasColorLayerEvidence =
            reliablePureRed || reliableWhiteCore;

        const bool hasComplexFlameVariation =
            !yellowDominant &&
            vStd >= (smallRegion ? 24.0 : 30.0) &&
            (
                maskChangeRatio >= (smallRegion ? 0.045 : 0.070) ||
                brightnessDiffMean >= (smallRegion ? 3.0 : 5.0)
                );

        // 밝은 배경에서는 화염의 주황/빨강 층과 흰 중심 대비가 약해질 수 있다.
        // 이 경우 색상 기준을 전역으로 낮추지 않고, 후보 자체의 지속적인
        // 밝기·형태 변화가 있을 때만 보조 화염 증거로 인정한다.
        const bool skinSafeBrightBackground =
            candidateSkinRatio < 0.48 ||
            rawCoreHaloEvidence ||
            skinSeparatedHaloEvidence;

        const bool brightBackgroundEvidence =
            meanV[0] >= 185.0 &&
            redOrangePixelCount >= (smallRegion ? 5 : 10) &&
            skinSafeBrightBackground &&
            (
                maskChangeRatio >= (smallRegion ? 0.055 : 0.080) ||
                brightnessDiffMean >= (smallRegion ? 3.5 : 5.5)
                );

        // 반사광은 흰색 중심은 강하지만, 화염 특유의 적색 층·중심 주변 고리와
        // 복합적인 시간 변화가 약한 경우가 많다. 이런 후보는 점수와 확정 기준을
        // 별도로 강화한다.
        const bool reflectionLikeCandidate =
            whiteCoreRatio >= (smallRegion ? 0.10 : 0.14) &&
            !coreHaloEvidence &&
            !reliablePureRed &&
            pureRedRatio < (smallRegion ? 0.045 : 0.060) &&
            redOrangeRatio < (smallRegion ? 0.32 : 0.36) &&
            vStd < (smallRegion ? 30.0 : 34.0) &&
            !hasComplexFlameVariation &&
            !brightBackgroundEvidence;

        // 기존 500px 기준은 사진처럼 약 20~30px 크기의 손가락 박스를
        // 일반 후보로 분류할 수 있었다. 작은 후보 범위를 넓혀 별도 확정
        // 기준을 적용한다.
        const int candidateShortSide =
            min(box.width, box.height);

        const bool tinyCandidate =
            boxArea < 1500.0 ||
            candidateShortSide < 28 ||
            firePixelCount < 100;

        // 작은 후보는 피부 마스크가 완벽하지 않아도 손가락일 가능성이 높다.
        const bool skinLikeCandidate =
            candidateSkinRatio >
            (tinyCandidate ? 0.12 : 0.35);

        // 피부 근처 후보는 밝기 변화만으로 구제하지 않는다.
        // 손가락 반사광도 밝기와 마스크 변화가 크게 나올 수 있으므로,
        // 피부 밖에 분리된 적색/주황색 층과 실제 화염 중심 구조를 함께 요구한다.
        const bool strictSkinSeparatedFlameEvidence =
            skinSeparatedHaloEvidence &&
            haloRedOrangeNonSkinPixelCount >= (smallRegion ? 5 : 10) &&
            (
                (
                    rawCoreHaloEvidence &&
                    reliableWhiteCore &&
                    redOrangePixelCount >= (smallRegion ? 10 : 18)
                    ) ||
                (
                    reliablePureRed &&
                    hasComplexFlameVariation
                    )
                );

        const bool independentFlameEvidence =
            (
                rawCoreHaloEvidence &&
                reliablePureRed &&
                (
                    hasComplexFlameVariation ||
                    brightnessDiffMean >= 3.2 ||
                    maskChangeRatio >= 0.045
                    )
                ) ||
            (
                reliableWhiteCore &&
                reliablePureRed &&
                hasComplexFlameVariation
                ) ||
            (
                brightBackgroundEvidence &&
                candidateSkinRatio < 0.20
                );

        const bool skinConnectedCandidate =
            candidateSkinRatio >= 0.35 &&
            (
                haloSkinRatio >= 0.55 ||
                surroundingSkinRatio >= 0.12
                );

        const bool skinSeparatedFlameEvidence =
            strictSkinSeparatedFlameEvidence ||
            (
                candidateSkinRatio < 0.20 &&
                independentFlameEvidence
                );

        // 피부와 연결된 작은 후보는 우선 손가락 위험 후보로 분류한다.
        // 실제 화염이면 아래의 엄격한 피부 분리 증거를 통해서만 구제된다.
        const bool fingerLikeCandidate =
            tinyCandidate &&
            skinConnectedCandidate;

        // 손가락 또는 반사광 위험 후보라고 해서 강한 화염 증거를
        // 무조건 제거하지 않는다. 실제 화염 구조와 시간 변화가 함께
        // 확인될 때만 제한적으로 구제하며, 이후 확정 단계에서 더 높은
        // 점수와 더 긴 연속 프레임 수를 요구한다.
        const bool reflectionFlameRescueEvidence =
            reflectionLikeCandidate &&
            redOrangePixelCount >= (smallRegion ? 6 : 12) &&
            redOrangeRatio >= (smallRegion ? 0.24 : 0.28) &&
            (
                brightnessDiffMean >= (smallRegion ? 2.8 : 4.0) ||
                maskChangeRatio >= (smallRegion ? 0.040 : 0.060)
                );

        const bool fingerFlameRescueEvidence =
            fingerLikeCandidate &&
            strictSkinSeparatedFlameEvidence &&
            reliableWhiteCore &&
            (
                reliablePureRed ||
                hasComplexFlameVariation
                );

        bool strongFireEvidence =
            hasColorLayerEvidence ||
            hasComplexFlameVariation ||
            rawCoreHaloEvidence ||
            brightBackgroundEvidence ||
            reflectionFlameRescueEvidence ||
            fingerFlameRescueEvidence;

        // 피부처럼 보이는 작은 후보는 단순한 빨강/흰색 픽셀 몇 개만으로
        // 화재 확정을 허용하지 않는다. 흰 중심을 주황/빨강 고리가 둘러싼
        // 실제 화염 구조가 있거나, 색상층과 복합 변화가 동시에 있어야 한다.
        if (skinLikeCandidate && tinyCandidate)
        {
            // 작은 피부 후보는 흰 점이나 주황색 몇 픽셀만으로 구제하지 않는다.
            // 피부 밖에 분리된 화염층이 확인된 경우에만 강한 화염 증거로 인정한다.
            if (!skinSeparatedFlameEvidence)
            {
                strongFireEvidence = false;
            }
        }
        else if (skinLikeCandidate &&
            !(reliableWhiteCore && reliablePureRed))
        {
            strongFireEvidence = false;
        }

        // 피부가 아니더라도 작은 후보는 실제 색상층 또는 중심-고리 구조가
        // 확인되어야 강한 화염 증거로 인정한다.
        if (tinyCandidate &&
            !hasColorLayerEvidence &&
            !coreHaloEvidence)
        {
            strongFireEvidence = false;
        }

        double yellowPenalty = 0.0;
        const double reflectionPenalty =
            reflectionLikeCandidate ? 0.12 : 0.0;

        const double fingerPenalty =
            fingerLikeCandidate ? 0.10 : 0.0;

        if (yellowDominant && weakFlameColorLayer)
        {
            yellowPenalty =
                uniformRigidYellow ? 0.22 : 0.12;
        }
        else if (yellowDominantRatio > 0.40 &&
            pureRedRatio < 0.040 &&
            whiteCoreRatio < 0.015)
        {
            yellowPenalty = 0.07;
        }

        // ==================================================
        // 12. 최종 점수
        // ==================================================
        const double colorScore =
            clamp01(
                colorRatio / 0.20
            );

        const double redScore =
            clamp01(
                redOrangeRatio / 0.35
            );

        const double whiteScore =
            clamp01(
                whiteCoreRatio / 0.12
            );

        const double textureScore =
            clamp01(
                vStd / 50.0
            );

        double finalScore =
            0.20 * colorScore +
            0.30 * redScore +
            0.20 * whiteScore +
            0.15 * textureScore +
            0.15 * dynamicScore;

        finalScore -= skinPenalty;
        finalScore -= yellowPenalty;
        finalScore -= reflectionPenalty;
        finalScore -= fingerPenalty;

        if (smallRegion)
        {
            finalScore += 0.02;
        }

        finalScore =
            clamp01(
                finalScore
            );

        if (smallRegion)
        {
            if (finalScore < 0.27)
                continue;
        }
        else
        {
            if (finalScore < 0.35)
                continue;
        }

        if (flickerScore > 0.22 ||
            flowScore > 0.30)
        {
            hasFlickerLikeMotion = true;
        }

        const double originalArea =
            contourAreaValue *
            scaleX *
            scaleY;

        totalFireArea +=
            originalArea;

        // ==================================================
        // 13. 원본 좌표 변환
        // ==================================================
        Rect originalBox(
            cvRound(box.x * scaleX),
            cvRound(box.y * scaleY),
            cvRound(box.width * scaleX),
            cvRound(box.height * scaleY)
        );

        const int paddingX =
            max(
                4,
                cvRound(originalBox.width * 0.12)
            );

        const int paddingY =
            max(
                4,
                cvRound(originalBox.height * 0.12)
            );

        originalBox.x -= paddingX;
        originalBox.y -= paddingY;
        originalBox.width += paddingX * 2;
        originalBox.height += paddingY * 2;

        originalBox &=
            Rect(
                0,
                0,
                inputFrame.cols,
                inputFrame.rows
            );

        if (originalBox.empty())
            continue;

        char labelBuffer[64];

        snprintf(
            labelBuffer,
            sizeof(labelBuffer),
            "FIRE SCORE %.2f",
            finalScore
        );

        DetectionBox detBox;

        detBox.box = originalBox;
        detBox.label = labelBuffer;
        detBox.type = DetectionType::FIRE;
        detBox.score = finalScore;
        detBox.strongFireEvidence = strongFireEvidence;
        detBox.yellowDominantRatio = yellowDominantRatio;
        detBox.tinyCandidate = tinyCandidate;
        detBox.skinLikeCandidate = skinLikeCandidate;
        detBox.coreHaloEvidence = coreHaloEvidence;
        detBox.reflectionLikeCandidate = reflectionLikeCandidate;
        detBox.brightBackgroundEvidence = brightBackgroundEvidence;
        detBox.fingerLikeCandidate = fingerLikeCandidate;
        detBox.skinSeparatedFlameEvidence = skinSeparatedFlameEvidence;
        detBox.brightnessDiffMean = brightnessDiffMean;
        detBox.maskChangeRatio = maskChangeRatio;
        detBox.whiteCoreRatio = whiteCoreRatio;
        detBox.redOrangeRatio = redOrangeRatio;
        detBox.pureRedRatio = pureRedRatio;
        detBox.candidateSkinRatio = candidateSkinRatio;
        detBox.surroundingSkinRatio = surroundingSkinRatio;
        detBox.haloSkinRatio = haloSkinRatio;
        detBox.boxAreaPixels = boxArea;
        detBox.firePixelCount = firePixelCount;
        detBox.pureRedPixelCount = pureRedPixelCount;
        detBox.whiteCorePixelCount = whiteCorePixelCount;

        acceptedBoxes.push_back(
            detBox
        );
    }

    // ==================================================
    // 14. 동일 위치 후보의 연속 프레임 확정
    // ==================================================
    const DetectionBox* highestCandidate = nullptr;
    const DetectionBox* trackedCandidate = nullptr;
    const DetectionBox* bestCandidate = nullptr;

    const auto continuesPreviousTrack =
        [&](const DetectionBox& candidateBox)
        {
            return
                !previousCandidateBox.empty() &&
                isSameCandidate(
                    previousCandidateBox,
                    candidateBox.box
                );
        };

    const auto requiredNewTrackScore =
        [](const DetectionBox& candidateBox)
        {
            if (candidateBox.fingerLikeCandidate)
                return 0.92;

            if (candidateBox.reflectionLikeCandidate)
                return 0.70;

            if (candidateBox.tinyCandidate)
                return 0.55;

            return NEW_TRACK_MIN_SCORE;
        };

    const auto canStartOrContinueTrack =
        [&](const DetectionBox& candidateBox)
        {
            if (continuesPreviousTrack(candidateBox))
                return true;

            return
                candidateBox.score >=
                requiredNewTrackScore(candidateBox);
        };

    // 후보별 새 트랙 기준을 먼저 적용한 뒤 최고 점수를 고른다.
    // 최고 점수 후보가 자기 기준을 통과하지 못했다고 해서 그보다 점수가
    // 조금 낮은 실제 화염 후보까지 함께 버리지 않도록 한다.
    for (const DetectionBox& candidateBox : acceptedBoxes)
    {
        if (!canStartOrContinueTrack(candidateBox))
            continue;

        if (highestCandidate == nullptr ||
            candidateBox.score > highestCandidate->score)
        {
            highestCandidate = &candidateBox;
        }

        if (continuesPreviousTrack(candidateBox) &&
            (
                trackedCandidate == nullptr ||
                candidateBox.score > trackedCandidate->score
                ))
        {
            trackedCandidate = &candidateBox;
        }
    }

    // 기존 추적 후보를 기본으로 유지하되, 새 후보 점수가 확실히 높으면
    // 실제 불꽃으로 추적 대상을 전환한다.
    constexpr double TRACK_SWITCH_SCORE_MARGIN = 0.12;

    if (trackedCandidate != nullptr &&
        highestCandidate != nullptr)
    {
        if (highestCandidate->score >=
            trackedCandidate->score + TRACK_SWITCH_SCORE_MARGIN)
        {
            bestCandidate = highestCandidate;
        }
        else
        {
            bestCandidate = trackedCandidate;
        }
    }
    else if (trackedCandidate != nullptr)
    {
        bestCandidate = trackedCandidate;
    }
    else
    {
        bestCandidate = highestCandidate;
    }

    int requiredConfirmFramesForUi = FIRE_CONFIRM_FRAMES;
    int requiredStrongFramesForUi = STRONG_CONFIRM_FRAMES;

    if (bestCandidate != nullptr)
    {
        const Rect& currentCandidateBox =
            bestCandidate->box;

        requiredConfirmFramesForUi =
            bestCandidate->fingerLikeCandidate ? 9 :
            (
                bestCandidate->reflectionLikeCandidate ? 7 :
                (bestCandidate->tinyCandidate ? 4 : FIRE_CONFIRM_FRAMES)
                );

        requiredStrongFramesForUi =
            bestCandidate->fingerLikeCandidate ? 6 :
            (
                bestCandidate->reflectionLikeCandidate ? 4 :
                (bestCandidate->tinyCandidate ? 2 : STRONG_CONFIRM_FRAMES)
                );

        const bool sameTrack =
            isSameCandidate(
                previousCandidateBox,
                currentCandidateBox
            );

        if (sameTrack)
        {
            // 후보 유형별 요구 프레임보다 카운터 상한이 낮아지는 모순을 막는다.
            const int confirmCounterLimit =
                max(
                    FIRE_CONFIRM_FRAMES + 4,
                    requiredConfirmFramesForUi + 2
                );

            fireConfirmCount =
                min(
                    fireConfirmCount + 1,
                    confirmCounterLimit
                );
        }
        else
        {
            // 새 위치 후보는 연속 카운트와 강한 증거를 다시 시작한다.
            fireConfirmCount = 1;
            strongFireCount = 0;
            weakKeepCount = 0;
            fireConfirmed = false;
        }

        candidateMissCount = 0;

        if (!fireConfirmed)
        {
            // 손 후보가 0.5~0.6 정도로 반복되더라도 화재로 확정되지 않도록
            // 0.68 이상의 강한 점수가 최소 2번 필요하다.
            const double requiredStrongScore =
                bestCandidate->fingerLikeCandidate ? 0.92 :
                (
                    bestCandidate->reflectionLikeCandidate ? 0.82 :
                    (bestCandidate->tinyCandidate ? 0.72 : STRONG_FIRE_SCORE)
                    );

            const int requiredStrongFrames =
                requiredStrongFramesForUi;

            const int requiredConfirmFrames =
                requiredConfirmFramesForUi;

            const bool passesTinySkinStructure =
                !bestCandidate->tinyCandidate ||
                !bestCandidate->skinLikeCandidate ||
                bestCandidate->skinSeparatedFlameEvidence;

            const bool reflectionDynamicRescue =
                bestCandidate->reflectionLikeCandidate &&
                bestCandidate->score >= 0.86 &&
                bestCandidate->redOrangeRatio >= 0.24 &&
                (
                    bestCandidate->brightnessDiffMean >= 2.8 ||
                    bestCandidate->maskChangeRatio >= 0.040
                    );

            const bool passesReflectionStructure =
                !bestCandidate->reflectionLikeCandidate ||
                bestCandidate->coreHaloEvidence ||
                bestCandidate->brightBackgroundEvidence ||
                reflectionDynamicRescue;

            const bool passesFingerStructure =
                !bestCandidate->fingerLikeCandidate ||
                bestCandidate->skinSeparatedFlameEvidence;

            if (bestCandidate->score >= requiredStrongScore &&
                bestCandidate->strongFireEvidence &&
                passesTinySkinStructure &&
                passesReflectionStructure &&
                passesFingerStructure)
            {
                strongFireCount =
                    min(
                        strongFireCount + 1,
                        max(8, requiredStrongFrames + 2)
                    );
            }
            else
            {
                strongFireCount =
                    max(
                        0,
                        strongFireCount - 1
                    );
            }

            if (fireConfirmCount >= requiredConfirmFrames &&
                strongFireCount >= requiredStrongFrames)
            {
                fireConfirmed = true;
                weakKeepCount = 0;
            }
        }
        else
        {
            // 확정 후에는 점수가 잠깐 내려가도 2회까지 유지하되,
            // 손 수준의 낮은 점수가 계속되면 화재 상태를 해제한다.
            const double requiredKeepScore =
                bestCandidate->fingerLikeCandidate ? 0.86 :
                (
                    bestCandidate->reflectionLikeCandidate ? 0.74 :
                    (bestCandidate->tinyCandidate ? 0.66 : KEEP_FIRE_SCORE)
                    );

            const bool keepsTinySkinStructure =
                !bestCandidate->tinyCandidate ||
                !bestCandidate->skinLikeCandidate ||
                bestCandidate->skinSeparatedFlameEvidence;

            const bool reflectionDynamicKeep =
                bestCandidate->reflectionLikeCandidate &&
                bestCandidate->score >= 0.78 &&
                bestCandidate->redOrangeRatio >= 0.22 &&
                (
                    bestCandidate->brightnessDiffMean >= 2.2 ||
                    bestCandidate->maskChangeRatio >= 0.032
                    );

            const bool keepsReflectionStructure =
                !bestCandidate->reflectionLikeCandidate ||
                bestCandidate->coreHaloEvidence ||
                bestCandidate->brightBackgroundEvidence ||
                reflectionDynamicKeep;

            const bool keepsFingerStructure =
                !bestCandidate->fingerLikeCandidate ||
                bestCandidate->skinSeparatedFlameEvidence;

            if (bestCandidate->score >= requiredKeepScore &&
                bestCandidate->strongFireEvidence &&
                keepsTinySkinStructure &&
                keepsReflectionStructure &&
                keepsFingerStructure)
            {
                weakKeepCount = 0;
            }
            else
            {
                ++weakKeepCount;

                if (weakKeepCount > MAX_WEAK_KEEP_FRAMES)
                {
                    fireConfirmed = false;
                    fireConfirmCount = 0;
                    strongFireCount = 0;
                    weakKeepCount = 0;
                }
            }
        }

        previousCandidateBox =
            currentCandidateBox;

    }
    else
    {
        if (!fireConfirmed)
        {
            ++candidateMissCount;

            // 확정 전 실제 화염이 한 번 끊겼다고 모든 누적값을 바로 지우지 않는다.
            // 한 번까지는 감쇠만 적용하고, 두 번 연속 누락되면 새 후보로 초기화한다.
            if (candidateMissCount <= MAX_PRECONFIRM_MISSES &&
                !previousCandidateBox.empty() &&
                fireConfirmCount > 0)
            {
                fireConfirmCount = max(0, fireConfirmCount - 1);
                strongFireCount = max(0, strongFireCount - 1);
                weakKeepCount = 0;
            }
            else
            {
                fireConfirmCount = 0;
                strongFireCount = 0;
                weakKeepCount = 0;
                candidateMissCount = 0;
                previousCandidateBox = Rect();
            }
        }
        else
        {
            ++candidateMissCount;

            if (candidateMissCount > MAX_CANDIDATE_MISSES)
            {
                fireConfirmed = false;
                fireConfirmCount = 0;
                strongFireCount = 0;
                weakKeepCount = 0;
                candidateMissCount = 0;
                previousCandidateBox = Rect();
            }
        }
    }

    const bool hasCurrentCandidate =
        bestCandidate != nullptr;

    // 중간 점수 후보는 candidate로만 남고, 강한 점수가 누적되어야 화재 확정.
    const bool detectedFire =
        fireConfirmed &&
        candidateMissCount <= MAX_CANDIDATE_MISSES;

    // candidate는 현재 프레임에 실제 후보가 있을 때만 true로 둔다.
    // 경보 상태(detected)는 순간 누락을 허용할 수 있지만,
    // 이전 박스를 현재 위치인 것처럼 다시 표시하지 않는다.
    result.candidate = hasCurrentCandidate;

    // 왼쪽 상단의 FIRE CANDIDATE 문구는 후보가 하나 생겼다고 바로 띄우지 않는다.
    // 정상 후보는 4회 중 3회 + 강한 증거 2회 중 1회,
    // 작은 후보는 4회 중 3회 + 강한 증거 2회 중 1회까지 왔을 때만 표시한다.
    result.candidateDisplayReady =
        hasCurrentCandidate &&
        bestCandidate != nullptr &&
        !bestCandidate->fingerLikeCandidate &&
        !detectedFire &&
        fireConfirmCount >= max(1, requiredConfirmFramesForUi - 1) &&
        strongFireCount >= max(1, requiredStrongFramesForUi - 1);

    result.detected = detectedFire;
    result.flicker = hasFlickerLikeMotion;
    result.area = totalFireArea;
    result.hitCount = fireConfirmCount;
    result.confirmCount = fireConfirmCount;

    // 현재 추적 중인 최적 후보 한 개만 표시한다.
    // 검출에는 여러 후보를 사용하지만 화면에 모든 contour 박스를
    // 그리지 않아 불꽃 주변에 박스가 여러 개 생기는 것을 막는다.
    result.boxes.clear();

    if (hasCurrentCandidate && bestCandidate != nullptr)
    {
        result.boxes.push_back(*bestCandidate);
    }

#if FIRE_DEBUG_VIEW
    // HighGUI(imshow/waitKey)는 메인 스레드에서만 처리한다.
    // 검출 스레드에서는 마스크를 결과 구조체에 복사해 전달한다.
    result.debugImages.fireColorMask = fireColorMask.clone();
    result.debugImages.skinMask = skinMask.clone();
    result.debugImages.foregroundMask = foregroundMask.clone();
    result.debugImages.candidateMask = candidateMask.clone();
#endif

    // ==================================================
    // 15. 다음 프레임용 상태 저장
    // ==================================================
    prevGray =
        gray.clone();

    prevVal =
        val.clone();

    prevFireMask =
        fireColorMask.clone();

    frameIndex++;

    return result;
}