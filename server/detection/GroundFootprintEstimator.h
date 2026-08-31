#pragma once

// 화재 표시 크기 추정. 바닥에 닿는 폭을 원의 지름으로 놓아 전광판 표시 반경을 계산한다.
// 카메라 한 대로는 화염 부피를 잴 수 없으므로 실제 연소 면적이 아닌 표시용 추정값이다.


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
