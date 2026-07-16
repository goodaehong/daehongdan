#pragma once

#include <opencv2/opencv.hpp>

// , ,   morphology   .
//     .
class FireMaskBuilder
{
public:
    FireMaskBuilder();

    void reset();

    cv::Mat makeSkinMask(
        const cv::Mat& frame,
        const cv::Mat& hsv
    ) const;

    cv::Mat makeFireColorMask(
        const cv::Mat& frame,
        const cv::Mat& hsv
    ) const;

    cv::Mat makeForegroundMask(
        const cv::Mat& frame,
        const cv::Mat& gray,
        int frameIndex
    );

    void cleanupBinaryMask(
        cv::Mat& mask,
        int openSize,
        int closeSize,
        int dilateSize
    ) const;

private:
    cv::Ptr<cv::BackgroundSubtractorMOG2> mog2_;
    cv::Mat prevGray_;
};