#include "IgnoreRegionFilter.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <opencv2/imgproc.hpp>

namespace
{
    double sanitizedThreshold(double threshold)
    {
        if (!std::isfinite(threshold)) return 0.5;
        return std::clamp(threshold, 0.0, 1.0);
    }
}

void IgnoreRegionFilter::setConfig(const IgnoreRegionConfig& config)
{
    IgnoreRegionConfig sanitized;
    sanitized.overlapThreshold = sanitizedThreshold(config.overlapThreshold);

    for (const IgnoreRegion& region : config.regions)
    {
        IgnoreRegion cleanRegion;
        cleanRegion.enabled = region.enabled;
        cleanRegion.points.reserve(region.points.size());

        for (const cv::Point2f& point : region.points)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) continue;
            cleanRegion.points.emplace_back(
                std::clamp(point.x, 0.0F, 1.0F),
                std::clamp(point.y, 0.0F, 1.0F));
        }

        if (cleanRegion.points.size() >= 3)
            sanitized.regions.push_back(std::move(cleanRegion));
    }

    config_ = std::move(sanitized);
    ignoreMask_.release();
    maskSize_ = {};
    maskValid_ = false;
}

void IgnoreRegionFilter::clear()
{
    setConfig(IgnoreRegionConfig{});
}

bool IgnoreRegionFilter::enabled() const
{
    return std::any_of(
        config_.regions.begin(),
        config_.regions.end(),
        [](const IgnoreRegion& region) {
            return region.enabled && region.points.size() >= 3;
        });
}

double IgnoreRegionFilter::overlapThreshold() const
{
    return config_.overlapThreshold;
}

void IgnoreRegionFilter::rebuildMask(const cv::Size& frameSize) const
{
    if (maskValid_ && maskSize_ == frameSize) return;

    maskSize_ = frameSize;
    maskValid_ = true;
    if (frameSize.width <= 0 || frameSize.height <= 0 || !enabled())
    {
        ignoreMask_.release();
        return;
    }

    ignoreMask_ = cv::Mat::zeros(frameSize, CV_8UC1);
    std::vector<std::vector<cv::Point>> polygons;

    for (const IgnoreRegion& region : config_.regions)
    {
        if (!region.enabled || region.points.size() < 3) continue;

        std::vector<cv::Point> polygon;
        polygon.reserve(region.points.size());
        for (const cv::Point2f& point : region.points)
        {
            polygon.emplace_back(
                cvRound(point.x * static_cast<float>(frameSize.width - 1)),
                cvRound(point.y * static_cast<float>(frameSize.height - 1)));
        }
        polygons.push_back(std::move(polygon));
    }

    if (!polygons.empty())
        cv::fillPoly(ignoreMask_, polygons, cv::Scalar(255));
}

bool IgnoreRegionFilter::shouldIgnore(
    const cv::Rect& detectionBox,
    const cv::Size& frameSize) const
{
    if (!enabled() || detectionBox.empty() ||
        frameSize.width <= 0 || frameSize.height <= 0)
        return false;

    rebuildMask(frameSize);
    if (ignoreMask_.empty()) return false;

    const cv::Rect clippedBox =
        detectionBox & cv::Rect(0, 0, frameSize.width, frameSize.height);
    if (clippedBox.empty()) return false;

    const double overlapArea =
        static_cast<double>(cv::countNonZero(ignoreMask_(clippedBox)));
    const double detectionArea = static_cast<double>(clippedBox.area());
    if (overlapArea <= 0.0 || detectionArea <= 0.0)
        return false;

    return overlapArea / detectionArea >= config_.overlapThreshold;
}

std::size_t IgnoreRegionFilter::filter(
    std::vector<DetectionBox>& detections,
    const cv::Size& frameSize) const
{
    if (!enabled() || detections.empty()) return 0;

    const std::size_t previousSize = detections.size();
    detections.erase(
        std::remove_if(
            detections.begin(),
            detections.end(),
            [&](const DetectionBox& detection) {
                return shouldIgnore(detection.box, frameSize);
            }),
        detections.end());
    return previousSize - detections.size();
}
