#include <opencv2/core.hpp>

#include "AppConfig.h"
#include "ConsoleFireApplication.h"

int main()
{
    cv::setNumThreads(flame_config::OPENCV_NUM_THREADS);

    ConsoleFireApplication application;
    return application.run();
}