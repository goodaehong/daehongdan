// 임시 디버그용: 이미지의 특정 좌표 픽셀 BGR 값 출력
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <iostream>

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    cv::Mat img = cv::imread(argv[1]);
    std::cout << "size: " << img.cols << "x" << img.rows << "\n";

    // image_to_bitmap.cpp와 동일한 파이프라인으로 장애물 마스크 만들고, 리사이즈 후 실제 회색조 값 확인
    cv::Mat whiteMask, orangeMask, greenMask;
    cv::inRange(img, cv::Scalar(200,200,200), cv::Scalar(255,255,255), whiteMask);
    cv::inRange(img, cv::Scalar(9,97,225), cv::Scalar(69,157,255), orangeMask);
    cv::inRange(img, cv::Scalar(0,200,151), cv::Scalar(59,255,211), greenMask);
    cv::Mat notObstacle, obstacleMask;
    cv::bitwise_or(whiteMask, orangeMask, notObstacle);
    cv::bitwise_or(notObstacle, greenMask, notObstacle);
    cv::bitwise_not(notObstacle, obstacleMask);

    cv::Mat resized;
    cv::resize(obstacleMask, resized, cv::Size(62,62), 0, 0, cv::INTER_AREA);

    std::cout << "--- 우측상단 사라진 벽 후보 col 38~48, row 5~11 raw값 (임계값 130 기준) ---\n";
    for (int gy = 5; gy <= 11; gy++)
    {
        std::cout << "row" << gy << ": ";
        for (int gx = 38; gx <= 48; gx++)
            std::cout << (int)resized.at<uint8_t>(gy, gx) << " ";
        std::cout << "\n";
    }

    std::cout << "--- 좌측 방 상단 가로벽 사라진 곳 col 0~14, row 18~23 raw값 ---\n";
    for (int gy = 18; gy <= 23; gy++)
    {
        std::cout << "row" << gy << ": ";
        for (int gx = 0; gx <= 14; gx++)
            std::cout << (int)resized.at<uint8_t>(gy, gx) << " ";
        std::cout << "\n";
    }
    return 0;
}
