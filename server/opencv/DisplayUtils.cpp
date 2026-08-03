#include "DisplayUtils.h"

#include <algorithm>

using namespace cv;
using namespace std;

void drawDetectionResult(Mat& display, const DetectionResult& result)
{
    for (const auto& box : result.boxes)
    {
        Scalar color;

        if (box.type == DetectionType::FIRE)
            color = Scalar(0, 0, 255);
        else
            color = Scalar(255, 255, 0);

        rectangle(display, box.box, color, 2);

        putText(display, box.label,
            Point(box.box.x, max(20, box.box.y - 8)),
            FONT_HERSHEY_SIMPLEX,
            0.55,
            color,
            2);
    }
}