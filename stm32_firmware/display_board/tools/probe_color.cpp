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
    cv::resize(obstacleMask, resized, cv::Size(64,64), 0, 0, cv::INTER_AREA);

    std::cout << "--- 리사이즈 후 grid row 5~13, col 58~63 raw값 (통로 후보) ---\n";
    for (int gy = 5; gy <= 13; gy++)
    {
        std::cout << "row" << gy << ": ";
        for (int gx = 58; gx <= 63; gx++)
            std::cout << (int)resized.at<uint8_t>(gy, gx) << " ";
        std::cout << "\n";
    }

    std::cout << "--- 문틀 벽 col 32~35, 42~45, row 1~8 raw값 (벽 비교용) ---\n";
    for (int gy = 1; gy <= 8; gy++)
    {
        std::cout << "row" << gy << ": ";
        for (int gx = 32; gx <= 35; gx++) std::cout << (int)resized.at<uint8_t>(gy, gx) << " ";
        std::cout << "| ";
        for (int gx = 42; gx <= 45; gx++) std::cout << (int)resized.at<uint8_t>(gy, gx) << " ";
        std::cout << "\n";
    }

    std::cout << "--- 하단 의자 틈 col 15~22, row 57~62 raw값 ---\n";
    for (int gy = 57; gy <= 62; gy++)
    {
        std::cout << "row" << gy << ": ";
        for (int gx = 15; gx <= 22; gx++) std::cout << (int)resized.at<uint8_t>(gy, gx) << " ";
        std::cout << "\n";
    }

    std::cout << "--- 하단 화장실 문틀 벽 col 25~42, row 49~62 raw값 (dilate 없음) ---\n";
    for (int gy = 49; gy <= 62; gy++)
    {
        std::cout << "row" << gy << ": ";
        for (int gx = 25; gx <= 42; gx++) std::cout << (int)resized.at<uint8_t>(gy, gx) << " ";
        std::cout << "\n";
    }

    // y=600 고정, x=200~350 넓게 스캔해서 실제 어두운(벽) 픽셀이 어디 있는지 찾기
    std::cout << "--- y=600, x=200~350 (검정에 가까운 픽셀만 W 표시) ---\n";
    for (int px = 200; px <= 350; px += 1)
    {
        cv::Vec3b v = img.at<cv::Vec3b>(600, px);
        bool dark = v[0] < 100 && v[1] < 100 && v[2] < 100;
        if (dark) std::cout << "x=" << px << " BGR=(" << (int)v[0] << "," << (int)v[1] << "," << (int)v[2] << ")\n";
    }

    // dilate 3px 적용 후 같은 영역 raw값 비교
    cv::Mat kernel3 = cv::Mat::ones(3, 3, CV_8U);
    cv::Mat dilated3;
    cv::dilate(obstacleMask, dilated3, kernel3);
    cv::Mat resized3;
    cv::resize(dilated3, resized3, cv::Size(64,64), 0, 0, cv::INTER_AREA);
    std::cout << "--- 하단 화장실 문틀 벽 col 25~42, row 49~62 raw값 (dilate 3px 적용) ---\n";
    for (int gy = 49; gy <= 62; gy++)
    {
        std::cout << "row" << gy << ": ";
        for (int gx = 25; gx <= 42; gx++) std::cout << (int)resized3.at<uint8_t>(gy, gx) << " ";
        std::cout << "\n";
    }
    return 0;
}
