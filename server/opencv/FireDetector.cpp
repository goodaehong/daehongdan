#include "FireDetector.h"

#include <algorithm>
#include <cmath>

using namespace cv;
using namespace std;

FireDetector::FireDetector()
{
    /*
     * GitHub 코드의 핵심 아이디어:
     * 고정 CCTV에서는 단순 absdiff보다 MOG2 배경차분이 안정적이다.
     *
     * history: 배경 모델 프레임 수
     * varThreshold: foreground 판단 민감도
     * detectShadows=false: 그림자를 foreground로 잡지 않게 함
     */
    mog2 = createBackgroundSubtractorMOG2(
        120,
        16.0,
        false
    );

    mog2->setDetectShadows(false);
}

void FireDetector::reset()
{
    prevGray.release();

    if (!mog2.empty())
    {
        mog2 = createBackgroundSubtractorMOG2(
            120,
            16.0,
            false
        );

        mog2->setDetectShadows(false);
    }

    frameIndex = 0;
    fireConfirmCount = 0;
}

Mat FireDetector::makeFireColorMask(const Mat& frame)
{
    Mat hsv;
    cvtColor(frame, hsv, COLOR_BGR2HSV);

    // ==================================================
    // 1. HSV 기반 화염색 후보
    // ==================================================
    Mat hsvFire1;
    Mat hsvFire2;
    Mat hsvFire;

    /*
     * OpenCV HSV 범위:
     * H: 0~179
     * S: 0~255
     * V: 0~255
     *
     * 0~42: 빨강~주황~노랑 계열
     * 170~179: 빨강 hue wrap-around 영역
     */
    inRange(
        hsv,
        Scalar(0, 60, 120),
        Scalar(42, 255, 255),
        hsvFire1
    );

    inRange(
        hsv,
        Scalar(170, 60, 120),
        Scalar(179, 255, 255),
        hsvFire2
    );

    bitwise_or(
        hsvFire1,
        hsvFire2,
        hsvFire
    );

    // ==================================================
    // 2. BGR 채널 관계 기반 화염색 후보
    // ==================================================
    vector<Mat> bgr;
    split(frame, bgr);

    Mat b = bgr[0];
    Mat g = bgr[1];
    Mat r = bgr[2];

    Mat rBright;
    Mat gBright;
    Mat rGeG;
    Mat gGtB;
    Mat rbDiff;
    Mat rbOk;

    /*
     * 화염은 보통 R이 강하고,
     * G도 어느 정도 있으며,
     * B는 상대적으로 낮다.
     */
    compare(r, 145, rBright, CMP_GT);
    compare(g, 50, gBright, CMP_GT);
    compare(r, g, rGeG, CMP_GE);
    compare(g, b, gGtB, CMP_GT);

    subtract(r, b, rbDiff);
    compare(rbDiff, 30, rbOk, CMP_GT);

    Mat bgrFire;

    bitwise_and(
        rBright,
        gBright,
        bgrFire
    );

    bitwise_and(
        bgrFire,
        rGeG,
        bgrFire
    );

    bitwise_and(
        bgrFire,
        gGtB,
        bgrFire
    );

    bitwise_and(
        bgrFire,
        rbOk,
        bgrFire
    );

    // HSV와 BGR 조건을 모두 만족해야 강한 화염색으로 인정
    Mat strictFire;

    bitwise_and(
        hsvFire,
        bgrFire,
        strictFire
    );

    // 작은 점 노이즈 제거
    Mat strictKernel =
        getStructuringElement(
            MORPH_ELLIPSE,
            Size(3, 3)
        );

    morphologyEx(
        strictFire,
        strictFire,
        MORPH_OPEN,
        strictKernel,
        Point(-1, -1),
        1
    );

    // ==================================================
    // 3. 흰색으로 날아간 화염 중심부
    // ==================================================
    Mat brightCore;

    /*
     * 밝고 채도가 낮은 흰색 영역.
     * 단독으로 쓰면 손톱, 조명, 반사광 오검출이 심하므로
     * 주변에 strictFire가 충분히 있을 때만 인정한다.
     */
    inRange(
        hsv,
        Scalar(0, 0, 235),
        Scalar(179, 60, 255),
        brightCore
    );

    Mat fireSupport;

    blur(
        strictFire,
        fireSupport,
        Size(11, 11)
    );

    /*
     * 주변 11x11 영역에서 화염색 픽셀이 일정 비율 이상 있을 때만
     * 흰색 중심부를 화염으로 인정.
     */
    threshold(
        fireSupport,
        fireSupport,
        45,
        255,
        THRESH_BINARY
    );

    Mat brightCoreNearFire;

    bitwise_and(
        brightCore,
        fireSupport,
        brightCoreNearFire
    );

    // ==================================================
    // 4. 최종 화염색 마스크
    // ==================================================
    Mat fireColorMask;

    bitwise_or(
        strictFire,
        brightCoreNearFire,
        fireColorMask
    );

    return fireColorMask;
}

Mat FireDetector::makeMotionMask(const Mat& frame)
{
    Mat gray;
    cvtColor(frame, gray, COLOR_BGR2GRAY);

    Mat motionMask =
        Mat::zeros(
            gray.size(),
            CV_8UC1
        );

    // ==================================================
    // 1. MOG2 배경차분
    // ==================================================
    Mat mogMask;

    if (!mog2.empty())
    {
        /*
         * 0.012는 GitHub 코드에서 쓰던 learningRate와 같은 계열.
         * 너무 크면 불꽃이 배경으로 빨리 흡수되고,
         * 너무 작으면 환경 변화에 둔감하다.
         */
        mog2->apply(
            frame,
            mogMask,
            0.012
        );

        threshold(
            mogMask,
            mogMask,
            254,
            255,
            THRESH_BINARY
        );

        /*
         * 초기 몇 프레임은 배경 모델이 안정화되지 않아서
         * 화면 전체가 움직임으로 잡힐 수 있다.
         */
        if (frameIndex >= 3)
        {
            bitwise_or(
                motionMask,
                mogMask,
                motionMask
            );
        }
    }

    // ==================================================
    // 2. 기존 방식: 이전 프레임과 absdiff
    // ==================================================
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
            motionMask,
            diffMask,
            motionMask
        );
    }

    prevGray = gray.clone();
    frameIndex++;

    // ==================================================
    // 3. 움직임 마스크 후처리
    // ==================================================
    Mat closeKernel =
        getStructuringElement(
            MORPH_ELLIPSE,
            Size(3, 3)
        );

    morphologyEx(
        motionMask,
        motionMask,
        MORPH_CLOSE,
        closeKernel,
        Point(-1, -1),
        1
    );

    /*
     * 불꽃은 외곽만 흔들리고 내부는 색상으로만 잡힐 수 있으므로
     * motionMask를 약간 확장한다.
     */
    Mat expandKernel =
        getStructuringElement(
            MORPH_ELLIPSE,
            Size(9, 9)
        );

    dilate(
        motionMask,
        motionMask,
        expandKernel,
        Point(-1, -1),
        1
    );

    return motionMask;
}

Mat FireDetector::makeSkinMask(const Mat& frame)
{
    // ==================================================
    // 1. YCrCb 피부색 후보
    // ==================================================
    Mat ycrcb;
    cvtColor(frame, ycrcb, COLOR_BGR2YCrCb);

    Mat skinYCrCb;

    inRange(
        ycrcb,
        Scalar(0, 133, 77),
        Scalar(255, 173, 127),
        skinYCrCb
    );

    // ==================================================
    // 2. HSV 피부색 후보
    // ==================================================
    Mat hsv;
    cvtColor(frame, hsv, COLOR_BGR2HSV);

    Mat skinHSV;

    inRange(
        hsv,
        Scalar(0, 20, 60),
        Scalar(25, 210, 255),
        skinHSV
    );

    /*
     * 두 색공간에서 모두 피부색일 때만 피부로 봄.
     * 손/손톱/피부 반사광 오검출 줄이기용.
     */
    Mat skinMask;

    bitwise_and(
        skinYCrCb,
        skinHSV,
        skinMask
    );

    Mat kernel =
        getStructuringElement(
            MORPH_ELLIPSE,
            Size(5, 5)
        );

    morphologyEx(
        skinMask,
        skinMask,
        MORPH_CLOSE,
        kernel,
        Point(-1, -1),
        1
    );

    return skinMask;
}

void FireDetector::densityDenoiseAndFill(
    Mat& mask,
    int ksize,
    int denoiseThreshold,
    int fillThreshold
)
{
    /*
     * GitHub 코드의 denoise/fill 아이디어를 단순화한 버전.
     *
     * 주변 ksize x ksize 안에 후보 픽셀이 너무 적으면 제거.
     * 주변에 후보 픽셀이 충분히 많으면 빈 공간을 채움.
     */

    Mat binary01;
    threshold(
        mask,
        binary01,
        0,
        1,
        THRESH_BINARY
    );

    Mat binary32;
    binary01.convertTo(
        binary32,
        CV_32S
    );

    Mat density;

    boxFilter(
        binary32,
        density,
        CV_32S,
        Size(ksize, ksize),
        Point(-1, -1),
        false
    );

    Mat denseEnough;
    compare(
        density,
        denoiseThreshold,
        denseEnough,
        CMP_GE
    );

    bitwise_and(
        mask,
        denseEnough,
        mask
    );

    Mat fillEnough;
    compare(
        density,
        fillThreshold,
        fillEnough,
        CMP_GE
    );

    bitwise_or(
        mask,
        fillEnough,
        mask
    );
}

void FireDetector::cleanupCandidateMask(Mat& mask)
{
    // 밀도 기반 노이즈 제거 + 빈 공간 채우기
    densityDenoiseAndFill(
        mask,
        7,
        5,
        18
    );

    medianBlur(
        mask,
        mask,
        3
    );

    Mat openKernel =
        getStructuringElement(
            MORPH_ELLIPSE,
            Size(3, 3)
        );

    Mat closeKernel =
        getStructuringElement(
            MORPH_ELLIPSE,
            Size(5, 5)
        );

    morphologyEx(
        mask,
        mask,
        MORPH_OPEN,
        openKernel,
        Point(-1, -1),
        1
    );

    morphologyEx(
        mask,
        mask,
        MORPH_CLOSE,
        closeKernel,
        Point(-1, -1),
        1
    );

    Mat expandKernel =
        getStructuringElement(
            MORPH_ELLIPSE,
            Size(5, 5)
        );

    dilate(
        mask,
        mask,
        expandKernel,
        Point(-1, -1),
        1
    );
}

DetectionResult FireDetector::detect(const Mat& inputFrame)
{
    DetectionResult result;

    if (inputFrame.empty())
        return result;

    const int FIRE_CONFIRM_FRAMES = 3;
    const int FIRE_DECAY = 1;

    // ==================================================
    // 1. 탐지용 영상 생성
    // ==================================================
    Mat frame;

    resize(
        inputFrame,
        frame,
        Size(960, 540),
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

    // ==================================================
    // 2. 화염색 / 움직임 / 피부 마스크 생성
    // ==================================================
    Mat fireColorMask =
        makeFireColorMask(frame);

    Mat motionMask =
        makeMotionMask(frame);

    Mat skinMask =
        makeSkinMask(frame);

    // ==================================================
    // 3. 후보 마스크 생성
    // ==================================================
    Mat candidateMask;

    /*
     * 화염색이면서 움직이는 영역만 후보.
     * 고정된 노란 물체, 조명, 벽, 안전표지판 오검출을 줄이는 핵심 조건.
     */
    bitwise_and(
        fireColorMask,
        motionMask,
        candidateMask
    );

    cleanupCandidateMask(
        candidateMask
    );

    // ==================================================
    // 4. HSV 채널 분리
    // ==================================================
    Mat hsv;
    cvtColor(frame, hsv, COLOR_BGR2HSV);

    vector<Mat> hsvChannels;
    split(
        hsv,
        hsvChannels
    );

    Mat hue = hsvChannels[0];
    Mat sat = hsvChannels[1];
    Mat val = hsvChannels[2];

    // ==================================================
    // 5. contour 추출
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

    // 큰 contour부터 검사
    sort(
        contours.begin(),
        contours.end(),
        [](const vector<Point>& a, const vector<Point>& b)
        {
            return contourArea(a) > contourArea(b);
        }
    );

    const double minArea =
        static_cast<double>(frame.cols * frame.rows) *
        0.00002;

    const size_t MAX_CONTOURS_TO_CHECK = 8;

    bool hasFireCandidate = false;
    bool hasFlickerLikeMotion = false;

    double totalFireArea = 0.0;

    vector<DetectionBox> candidateBoxes;

    const size_t contourLimit =
        min(
            contours.size(),
            MAX_CONTOURS_TO_CHECK
        );

    for (size_t i = 0; i < contourLimit; ++i)
    {
        const double area =
            contourArea(contours[i]);

        if (area < minArea)
            continue;

        Rect box =
            boundingRect(contours[i]);

        // 상단 날짜/시간/OSD 오검출 방지
        if (box.y <
            static_cast<int>(frame.rows * 0.10))
        {
            continue;
        }

        if (box.width < 3 ||
            box.height < 3)
        {
            continue;
        }

        box &= Rect(
            0,
            0,
            frame.cols,
            frame.rows
        );

        if (box.empty())
            continue;

        const double boxArea =
            static_cast<double>(box.width) *
            static_cast<double>(box.height);

        if (boxArea <= 0.0)
            continue;

        Mat roiFire =
            fireColorMask(box);

        Mat roiCandidate =
            candidateMask(box);

        Mat roiMotion =
            motionMask(box);

        Mat roiSkin =
            skinMask(box);

        Mat roiH =
            hue(box);

        Mat roiS =
            sat(box);

        Mat roiV =
            val(box);

        const int firePixelCount =
            countNonZero(roiFire);

        const int candidatePixelCount =
            countNonZero(roiCandidate);

        const int motionPixelCount =
            countNonZero(roiMotion);

        if (candidatePixelCount <= 0 ||
            firePixelCount <= 0)
        {
            continue;
        }

        const double colorRatio =
            static_cast<double>(firePixelCount) /
            boxArea;

        const double candidateRatio =
            static_cast<double>(candidatePixelCount) /
            boxArea;

        const double motionRatio =
            static_cast<double>(motionPixelCount) /
            boxArea;

        if (colorRatio < 0.05)
            continue;

        if (candidateRatio < 0.01)
            continue;

        if (motionRatio < 0.005)
            continue;

        // ==================================================
        // 6. 노란색 단색 물체 제거
        // ==================================================
        Mat redOrangeHue1;
        Mat redOrangeHue2;
        Mat redOrangeHue;

        inRange(
            roiH,
            Scalar(0),
            Scalar(18),
            redOrangeHue1
        );

        inRange(
            roiH,
            Scalar(170),
            Scalar(179),
            redOrangeHue2
        );

        bitwise_or(
            redOrangeHue1,
            redOrangeHue2,
            redOrangeHue
        );

        Mat redOrangeSat;
        Mat redOrangeVal;
        Mat redOrangeFire;

        compare(
            roiS,
            70,
            redOrangeSat,
            CMP_GT
        );

        compare(
            roiV,
            140,
            redOrangeVal,
            CMP_GT
        );

        bitwise_and(
            redOrangeHue,
            redOrangeSat,
            redOrangeFire
        );

        bitwise_and(
            redOrangeFire,
            redOrangeVal,
            redOrangeFire
        );

        bitwise_and(
            redOrangeFire,
            roiFire,
            redOrangeFire
        );

        // 흰색으로 날아간 화염 중심부
        Mat whiteCoreSat;
        Mat whiteCoreVal;
        Mat whiteCoreFire;

        compare(
            roiS,
            55,
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
            roiFire,
            whiteCoreFire
        );

        const double redOrangeRatio =
            static_cast<double>(
                countNonZero(redOrangeFire)
                ) /
            static_cast<double>(
                max(1, firePixelCount)
                );

        const double whiteCoreRatio =
            static_cast<double>(
                countNonZero(whiteCoreFire)
                ) /
            static_cast<double>(
                max(1, firePixelCount)
                );

        /*
         * 빨강/주황도 거의 없고,
         * 흰색 중심부도 없으면
         * 단순 노란 물체일 가능성이 높다.
         */
        if (redOrangeRatio < 0.05 &&
            whiteCoreRatio < 0.015)
        {
            continue;
        }

        // ==================================================
        // 7. 뜨거운 화염 픽셀 비율 확인
        // ==================================================
        Mat hotSat;
        Mat hotVal;
        Mat orangeHot;

        compare(
            roiS,
            80,
            hotSat,
            CMP_GT
        );

        compare(
            roiV,
            150,
            hotVal,
            CMP_GT
        );

        bitwise_and(
            hotSat,
            hotVal,
            orangeHot
        );

        bitwise_and(
            orangeHot,
            roiFire,
            orangeHot
        );

        Mat hotFire;

        bitwise_or(
            orangeHot,
            whiteCoreFire,
            hotFire
        );

        const double hotRatio =
            static_cast<double>(
                countNonZero(hotFire)
                ) /
            boxArea;

        if (hotRatio < 0.01)
            continue;

        // ==================================================
        // 8. 피부/손톱/반사광 제거
        // ==================================================
        Mat skinCandidateOverlap;

        bitwise_and(
            roiSkin,
            roiCandidate,
            skinCandidateOverlap
        );

        const double candidateSkinRatio =
            static_cast<double>(
                countNonZero(skinCandidateOverlap)
                ) /
            static_cast<double>(
                max(1, candidatePixelCount)
                );

        Mat lowSatMask;
        Mat veryBrightMask;
        Mat reflectionMask;

        compare(
            roiS,
            55,
            lowSatMask,
            CMP_LT
        );

        compare(
            roiV,
            230,
            veryBrightMask,
            CMP_GT
        );

        bitwise_and(
            lowSatMask,
            veryBrightMask,
            reflectionMask
        );

        const double reflectionRatio =
            static_cast<double>(
                countNonZero(reflectionMask)
                ) /
            boxArea;

        Mat strongSatMask;
        Mat strongValMask;
        Mat strongOrangeMask;

        compare(
            roiS,
            110,
            strongSatMask,
            CMP_GT
        );

        compare(
            roiV,
            170,
            strongValMask,
            CMP_GT
        );

        bitwise_and(
            strongSatMask,
            strongValMask,
            strongOrangeMask
        );

        bitwise_and(
            strongOrangeMask,
            roiFire,
            strongOrangeMask
        );

        const double strongOrangeRatio =
            static_cast<double>(
                countNonZero(strongOrangeMask)
                ) /
            boxArea;

        /*
         * 피부 후보와 많이 겹치는데,
         * 실제 고채도 주황색 화염 픽셀이 부족하면 제거.
         */
        if (candidateSkinRatio > 0.55 &&
            strongOrangeRatio < 0.08)
        {
            continue;
        }

        /*
         * 흰색 반사광이 많은데,
         * 고채도 주황 화염이 부족하면 제거.
         */
        if (reflectionRatio > 0.18 &&
            strongOrangeRatio < 0.05)
        {
            continue;
        }

        // ==================================================
        // 9. 형태 조건
        // ==================================================
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

        // 너무 길쭉한 선/반사광/글자 제거
        if (aspect > 5.0)
            continue;

        // ==================================================
// 9-1. 단색 주황 물체 제거
// 진짜 불은 ROI 내부 밝기 변화가 크고,
// 단색 물체는 V 채널 변화가 작다.
// ==================================================
        Scalar meanV;
        Scalar stdV;

        meanStdDev(
            roiV,
            meanV,
            stdV,
            roiFire
        );

        const double vStd =
            stdV[0];

        if (vStd < 18.0)
        {
            continue;
        }

        hasFireCandidate = true;

        if (motionRatio > 0.02)
            hasFlickerLikeMotion = true;

        const double originalArea =
            area * scaleX * scaleY;

        totalFireArea += originalArea;

        // ==================================================
        // 10. 원본 좌표로 변환
        // ==================================================
        Rect originalBox(
            cvRound(box.x * scaleX),
            cvRound(box.y * scaleY),
            cvRound(box.width * scaleX),
            cvRound(box.height * scaleY)
        );

        const int paddingX =
            cvRound(originalBox.width * 0.10);

        const int paddingY =
            cvRound(originalBox.height * 0.10);

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

        DetectionBox detBox;

        detBox.box = originalBox;
        detBox.label = "FIRE";
        detBox.type = DetectionType::FIRE;
        detBox.score = originalArea;

        candidateBoxes.push_back(detBox);
    }

    // ==================================================
    // 11. confirm frame 처리
    // ==================================================
    if (hasFireCandidate)
    {
        fireConfirmCount =
            min(
                fireConfirmCount + 1,
                FIRE_CONFIRM_FRAMES
            );
    }
    else
    {
        fireConfirmCount =
            max(
                0,
                fireConfirmCount - FIRE_DECAY
            );
    }

    result.candidate = hasFireCandidate;
    result.detected = fireConfirmCount >= FIRE_CONFIRM_FRAMES;
    result.flicker = hasFlickerLikeMotion;

    result.area = totalFireArea;
    result.hitCount = fireConfirmCount;
    result.confirmCount = fireConfirmCount;

    /*
     * 박스는 candidate 상태에서도 반환한다.
     * 화면 표시에서 확정 감지만 그리고 싶으면
     * 호출부에서 result.detected == true일 때만 그리면 된다.
     */
    result.boxes = candidateBoxes;

    return result;
}