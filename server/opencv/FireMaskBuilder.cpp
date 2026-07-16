#include "FireMaskBuilder.h"

#include <vector>

using namespace cv;
using namespace std;

FireMaskBuilder::FireMaskBuilder() { reset(); }

void FireMaskBuilder::reset()
{
    prevGray_.release();
    mog2_ = createBackgroundSubtractorMOG2(200, 16.0, false);
    mog2_->setDetectShadows(false);
}

void FireMaskBuilder::cleanupBinaryMask(Mat& mask, int openSize, int closeSize, int dilateSize) const
{
    if (mask.empty()) return;

    if (openSize > 1)
    {
        const Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(openSize, openSize));
        morphologyEx(mask, mask, MORPH_OPEN, kernel);
    }
    if (closeSize > 1)
    {
        const Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(closeSize, closeSize));
        morphologyEx(mask, mask, MORPH_CLOSE, kernel);
    }
    if (dilateSize > 1)
    {
        const Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(dilateSize, dilateSize));
        dilate(mask, mask, kernel);
    }
}

Mat FireMaskBuilder::makeSkinMask(const Mat& frame, const Mat& hsv) const
{
    Mat ycrcb, skinYCrCb, skinHSV, skinMask;
    cvtColor(frame, ycrcb, COLOR_BGR2YCrCb);
    inRange(ycrcb, Scalar(0, 133, 77), Scalar(255, 180, 135), skinYCrCb);
    inRange(hsv, Scalar(0, 20, 50), Scalar(25, 230, 255), skinHSV);
    bitwise_and(skinYCrCb, skinHSV, skinMask);

    morphologyEx(skinMask, skinMask, MORPH_CLOSE,
        getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
    dilate(skinMask, skinMask, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)));
    return skinMask;
}

Mat FireMaskBuilder::makeFireColorMask(const Mat& frame, const Mat& hsv) const
{
    vector<Mat> hsvCh, bgrCh;
    split(hsv, hsvCh);
    split(frame, bgrCh);
    const Mat& s = hsvCh[1];
    const Mat& v = hsvCh[2];
    const Mat& b = bgrCh[0];
    const Mat& g = bgrCh[1];
    const Mat& r = bgrCh[2];

    Mat hsvFire1, hsvFire2, hsvFire;
    inRange(hsv, Scalar(0, 56, 111), Scalar(35, 255, 255), hsvFire1);
    inRange(hsv, Scalar(170, 56, 111), Scalar(179, 255, 255), hsvFire2);
    bitwise_or(hsvFire1, hsvFire2, hsvFire);

    Mat rBright, gBright, rbDiff, gbDiff, rgDiff, rbOk, gbOk, rgOk, bgrFire;
    compare(r, 125, rBright, CMP_GT);
    compare(g, 45, gBright, CMP_GT);
    subtract(r, b, rbDiff);
    subtract(g, b, gbDiff);
    subtract(r, g, rgDiff);
    compare(rbDiff, 40, rbOk, CMP_GT);
    compare(gbDiff, 8, gbOk, CMP_GT);
    compare(rgDiff, 5, rgOk, CMP_GT);
    bitwise_and(rBright, gBright, bgrFire);
    bitwise_and(bgrFire, rbOk, bgrFire);
    bitwise_and(bgrFire, gbOk, bgrFire);
    bitwise_and(bgrFire, rgOk, bgrFire);

    Mat chromaticFire;
    bitwise_and(hsvFire, bgrFire, chromaticFire);

    Mat whiteCore, fireHalo, whiteCoreNearFire, fireColorMask;
    inRange(hsv, Scalar(0, 0, 226), Scalar(179, 64, 255), whiteCore);
    dilate(chromaticFire, fireHalo, getStructuringElement(MORPH_ELLIPSE, Size(9, 9)));
    bitwise_and(whiteCore, fireHalo, whiteCoreNearFire);
    bitwise_or(chromaticFire, whiteCoreNearFire, fireColorMask);
    cleanupBinaryMask(fireColorMask, 3, 3, 1);
    return fireColorMask;
}

Mat FireMaskBuilder::makeForegroundMask(const Mat& frame, const Mat& gray, int frameIndex)
{
    Mat foreground = Mat::zeros(gray.size(), CV_8UC1);

    if (!mog2_.empty())
    {
        Mat mogMask;
        // Slow background learning reduces absorption of a flame that stays in one place.
        mog2_->apply(frame, mogMask, 0.001);
        threshold(mogMask, mogMask, 220, 255, THRESH_BINARY);
        if (frameIndex >= 8) bitwise_or(foreground, mogMask, foreground);
    }

    if (!prevGray_.empty() && prevGray_.size() == gray.size())
    {
        Mat diff, diffMask;
        absdiff(prevGray_, gray, diff);
        threshold(diff, diffMask, 10, 255, THRESH_BINARY);
        bitwise_or(foreground, diffMask, foreground);
    }

    cleanupBinaryMask(foreground, 3, 3, 3);
    prevGray_ = gray.clone();
    return foreground;
}
