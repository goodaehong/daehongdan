#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/opencv.hpp>

enum class StreamSourceType
{
    RtspCamera,
    VideoFile
};

class CameraStream
{
public:
    CameraStream(
        const std::string& source,
        StreamSourceType sourceType,
        bool loopVideoFile = true
    );
    ~CameraStream();

    bool start();
    void stop();

    bool getLatestFrame(cv::Mat& outFrame, std::uint64_t& lastFrameId);
    bool isOpened() const;

private:
    void readLoop();
    bool openSource();

private:
    std::string source_;
    StreamSourceType sourceType_;
    bool loopVideoFile_;

    cv::VideoCapture cap_;
    std::thread readerThread_;
    std::mutex frameMutex_;
    cv::Mat latestFrame_;
    std::uint64_t latestFrameId_ = 0;

    std::atomic<bool> running_{ false };
    std::atomic<bool> opened_{ false };
    std::atomic<bool> hasFrame_{ false };
};