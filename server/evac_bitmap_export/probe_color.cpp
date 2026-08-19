// 임시 진단용: 60x60 리사이즈 후 특정 좌표의 raw(threshold 적용 전) 그레이스케일 값 확인
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <opencv2/opencv.hpp>
#include <iostream>

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    cv::Mat img = cv::imread(argv[1]);
    std::cout << "size: " << img.cols << "x" << img.rows << "\n";

    cv::Mat whiteMask, orangeMask, greenMask;
    cv::inRange(img, cv::Scalar(200,200,200), cv::Scalar(255,255,255), whiteMask);
    cv::inRange(img, cv::Scalar(9,97,225), cv::Scalar(69,157,255), orangeMask);
    cv::inRange(img, cv::Scalar(0,200,151), cv::Scalar(59,255,211), greenMask);
    cv::Mat notObstacle, obstacleMask;
    cv::bitwise_or(whiteMask, orangeMask, notObstacle);
    cv::bitwise_or(notObstacle, greenMask, notObstacle);
    cv::bitwise_not(notObstacle, obstacleMask);

    cv::Mat resized;
    cv::resize(obstacleMask, resized, cv::Size(60,60), 0, 0, cv::INTER_AREA);

    std::cout << "--- row 3~9, col 28~35 raw값 (row5~7에서 사라진 세로벽 후보, 임계값 125 기준) ---\n";
    for (int gy = 3; gy <= 9; gy++)
    {
        std::cout << "row" << gy << ": ";
        for (int gx = 28; gx <= 35; gx++)
            std::cout << (int)resized.at<uint8_t>(gy, gx) << " ";
        std::cout << "\n";
    }
    return 0;
}
