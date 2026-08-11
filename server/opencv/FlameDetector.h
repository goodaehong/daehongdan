#pragma once

#include <deque>
#include <vector>

#include <opencv2/opencv.hpp>
#include <opencv2/ml.hpp>

#include "DetectionTypes.h"
#include "IgnoreRegionFilter.h"

// RGB/HSV 색상, MOG2 움직임, 형태·질감 특징과 시간 추적을 결합한 화염 검출기다.
// 연기 검출은 포함하지 않으며 SmokeDetector가 별도로 담당한다.
class FlameDetector
{
public:
    FlameDetector();

    DetectionResult detect(
        const cv::Mat& inputFrame,
        std::uint64_t frameId = 0,
        std::int64_t timestampMs = 0);
    // UI/서버가 전달한 활성 영역만 적용한다. 빈 설정이면 기존 감지와 동일하다.
    void setIgnoreRegionConfig(const IgnoreRegionConfig& config);
    void reset();

private:
    struct Features
    {
        // analyzeContour가 후보 하나에서 추출해 규칙식 또는 선택적 SVM에 전달한다.
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

        // 후보 밝기와 주변 링 밝기의 차이는 약한 가감점으로만 사용한다.
        double candidateBrightness = 0.0;
        double surroundingBrightness = -1.0;
        double brightnessDelta = 0.0;
        double brightnessRatio = 1.0;
        double relativeBrightnessScore = 0.0;

        cv::Mat svmRow() const;
    };

    struct Track
    {
        // 같은 위치의 후보를 여러 분석 프레임에 걸쳐 확인하는 내부 추적 상태다.
        int id = -1;
        cv::Rect box;
        cv::Rect firstBox;
        std::uint64_t firstSeenFrameId = 0;
        std::int64_t firstSeenTimestampMs = 0;
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

    std::vector<DetectionBox> updateTracks(
        const std::vector<Features>& detections,
        const cv::Size& frameSize,
        std::uint64_t frameId,
        std::int64_t timestampMs);
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
    IgnoreRegionFilter ignoreRegionFilter_;

    int frameIndex_ = 0;
    int nextTrackId_ = 1;

    cv::Mat kernel3_;
    cv::Mat kernel5_;
    cv::Mat kernel7_;
};
