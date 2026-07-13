#pragma once

#include <opencv2/opencv.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class CameraStream
{
private:
    std::string url;
    cv::VideoCapture cap;

    std::thread readerThread;
    std::mutex frameMutex;

    cv::Mat latestFrame;
    std::uint64_t latestFrameId = 0;

    std::atomic<bool> running{ false };
    std::atomic<bool> opened{ false };
    std::atomic<bool> hasFrame{ false };

private:
    void readLoop();

public:
    explicit CameraStream(const std::string& rtspUrl);
    ~CameraStream();

    bool start();
    void stop();

    // 새 프레임일 때만 true를 반환한다.
    bool getLatestFrame(
        cv::Mat& outFrame,
        std::uint64_t& lastFrameId
    );

    bool isOpened() const;
};