#pragma once

#include <deque>
#include <vector>

#include <opencv2/opencv.hpp>
#include <opencv2/ml.hpp>

#include "DetectionTypes.h"

class FlameDetector
{
public:
    FlameDetector();

    DetectionResult detect(const cv::Mat& inputFrame);
    void reset();

private:
    struct Features
    {
        cv::Rect box;
        double score = 0.0;
        double colorCoverage = 0.0;
        double motionCoverage = 0.0;
        double redOrangeCoverage = 0.0;
        double whiteCoreCoverage = 0.0;
        double skinCoverage = 0.0;
        double vStd = 0.0;
        double circularity = 0.0;
        double solidity = 0.0;
        double extent = 0.0;
        double roughness = 0.0;
        double textureEntropy = 0.0;
        double textureEnergy = 0.0;
        double maskChange = 0.0;

        cv::Mat svmRow() const;
    };

    struct Track
    {
        int id = -1;
        cv::Rect box;
        int hits = 0;
        int misses = 0;
        int strongHits = 0;
        double score = 0.0;
        bool confirmed = false;
        std::deque<double> areaHistory;

        double colorCoverage = 0.0;
        double motionCoverage = 0.0;
        double redOrangeCoverage = 0.0;
        double whiteCoreCoverage = 0.0;
        double skinCoverage = 0.0;
        double vStd = 0.0;
        double maskChange = 0.0;
    };

    cv::Mat buildMotionMask(const cv::Mat& frame, const cv::Mat& gray);
    cv::Mat buildOriginalColorMask(const cv::Mat& frame, const cv::Mat& motionMask) const;
    cv::Mat buildSkinMask(const cv::Mat& frame, const cv::Mat& hsv) const;
    cv::Mat buildWhiteCoreMask(const cv::Mat& hsv, const cv::Mat& colorMask) const;

    Features analyzeContour(
        const std::vector<cv::Point>& contour,
        const cv::Mat& gray,
        const cv::Mat& hue,
        const cv::Mat& value,
        const cv::Mat& colorMask,
        const cv::Mat& motionMask,
        const cv::Mat& candidateMask,
        const cv::Mat& skinMask,
        const cv::Mat& whiteCoreMask
    ) const;

    void calculateGlcm(
        const cv::Mat& gray,
        const cv::Mat& mask,
        double& entropy,
        double& energy
    ) const;

    std::vector<DetectionBox> updateTracks(const std::vector<Features>& detections);
    static double intersectionOverUnion(const cv::Rect& a, const cv::Rect& b);
    static bool sameTarget(const cv::Rect& a, const cv::Rect& b);
    double classify(const Features& features) const;

private:
    cv::Ptr<cv::BackgroundSubtractorMOG2> mog2_;
    cv::Ptr<cv::ml::SVM> svm_;
    bool svmReady_ = false;

    cv::Mat previousGray_;
    cv::Mat previousCandidateMask_;
    std::vector<Track> tracks_;

    int frameIndex_ = 0;
    int nextTrackId_ = 1;

    cv::Mat kernel3_;
    cv::Mat kernel5_;
    cv::Mat kernel7_;
};