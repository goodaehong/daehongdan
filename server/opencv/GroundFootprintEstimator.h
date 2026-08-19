#pragma once

#include <algorithm>
#include <cmath>

#include <opencv2/core.hpp>

struct GroundFootprintEstimate
{
    bool valid = false;
    double widthMetres = 0.0;
    double areaSquareMetres = 0.0;
    int displayRadiusCells = 0;
};

// A monocular ground Homography can measure the mapped fire-box base width,
// but not flame volume.  For display purposes we explicitly model that width
// as the diameter of a circular floor footprint.  This is an estimate, not a
// directly measured burn area.
inline GroundFootprintEstimate estimateCircularGroundFootprint(
    const cv::Point2f& leftFactoryM,
    const cv::Point2f& rightFactoryM,
    const cv::Point& leftGrid,
    const cv::Point& rightGrid)
{
    constexpr double PI = 3.14159265358979323846;
    GroundFootprintEstimate estimate;
    const double widthMetres = cv::norm(rightFactoryM - leftFactoryM);
    if (!std::isfinite(widthMetres) || widthMetres <= 0.0) return estimate;

    estimate.valid = true;
    estimate.widthMetres = widthMetres;
    estimate.areaSquareMetres = PI * widthMetres * widthMetres * 0.25;

    const double gridDiameter = std::hypot(
        static_cast<double>(rightGrid.x - leftGrid.x),
        static_cast<double>(rightGrid.y - leftGrid.y));
    estimate.displayRadiusCells = std::clamp(
        static_cast<int>(std::ceil(gridDiameter * 0.5)), 1, 59);
    return estimate;
}
