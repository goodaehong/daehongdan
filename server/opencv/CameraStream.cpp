#include "CameraStream.h"

#include <chrono>
#include <iostream>
#include <vector>

using namespace cv;
using namespace std;

CameraStream::CameraStream(
    const string& source,
    StreamSourceType sourceType,
    bool loopVideoFile)
    : source_(source),
    sourceType_(sourceType),
    loopVideoFile_(loopVideoFile)
{
}

CameraStream::~CameraStream()
{
    stop();
}

bool CameraStream::start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return false;
    if (readerThread_.joinable()) readerThread_.join();

    {
        lock_guard<mutex> lock(frameMutex_);
        latestFrame_.release();
        latestFrameId_ = 0;
        hasFrame_ = false;
    }

    opened_ = false;
    readerThread_ = thread(&CameraStream::readLoop, this);
    return true;
}

void CameraStream::stop()
{
    running_ = false;
    if (readerThread_.joinable()) readerThread_.join();
    if (cap_.isOpened()) cap_.release();
    opened_ = false;
    hasFrame_ = false;
}

bool CameraStream::isOpened() const
{
    return opened_.load();
}

bool CameraStream::getLatestFrame(Mat& outFrame, uint64_t& lastFrameId)
{
    lock_guard<mutex> lock(frameMutex_);
    if (!hasFrame_.load() || latestFrame_.empty() || latestFrameId_ == lastFrameId)
        return false;

    outFrame = latestFrame_;
    lastFrameId = latestFrameId_;
    return true;
}

bool CameraStream::openSource()
{
    if (cap_.isOpened()) cap_.release();

    if (sourceType_ == StreamSourceType::VideoFile)
    {
        cout << "Opening video file: " << source_ << endl;
        if (!cap_.open(source_, CAP_FFMPEG))
        {
            cap_.release();
            if (!cap_.open(source_, CAP_ANY))
            {
                cerr << "Video file open failed: " << source_ << endl;
                return false;
            }
        }

        opened_ = true;
        cout << "Video file opened | "
            << static_cast<int>(cap_.get(CAP_PROP_FRAME_WIDTH)) << 'x'
            << static_cast<int>(cap_.get(CAP_PROP_FRAME_HEIGHT))
            << " | FPS " << cap_.get(CAP_PROP_FPS) << endl;
        return true;
    }

    const vector<int> openParams = {
        CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
        CAP_PROP_READ_TIMEOUT_MSEC, 2000
    };

    cout << "Camera connecting..." << endl;
    if (!cap_.open(source_, CAP_FFMPEG, openParams))
    {
        cerr << "Camera open failed" << endl;
        return false;
    }

    if (!cap_.set(CAP_PROP_BUFFERSIZE, 1))
        cout << "Camera buffer size option is not supported by this backend" << endl;

    opened_ = true;
    cout << "Camera connected" << endl;
    return true;
}

void CameraStream::readLoop()
{
    chrono::steady_clock::time_point nextVideoFrameTime;
    chrono::steady_clock::duration videoFrameInterval = chrono::milliseconds(33);

    while (running_.load())
    {
        if (!cap_.isOpened())
        {
            if (!openSource())
            {
                opened_ = false;
                this_thread::sleep_for(chrono::milliseconds(500));
                continue;
            }

            if (sourceType_ == StreamSourceType::VideoFile)
            {
                double fps = cap_.get(CAP_PROP_FPS);
                if (fps <= 1.0 || fps > 240.0) fps = 30.0;
                videoFrameInterval = chrono::duration_cast<chrono::steady_clock::duration>(
                    chrono::duration<double>(1.0 / fps));
                nextVideoFrameTime = chrono::steady_clock::now();
            }
        }

        Mat frame;
        if (!cap_.read(frame) || frame.empty())
        {
            if (sourceType_ == StreamSourceType::VideoFile)
            {
                if (loopVideoFile_)
                {
                    cout << "Video finished. Restarting from beginning." << endl;
                    cap_.set(CAP_PROP_POS_FRAMES, 0);
                    nextVideoFrameTime = chrono::steady_clock::now();
                    continue;
                }

                cout << "Video playback finished." << endl;
                running_ = false;
                opened_ = false;
                break;
            }

            opened_ = false;
            cout << "Camera read failed. Reconnecting..." << endl;
            cap_.release();
            this_thread::sleep_for(chrono::milliseconds(200));
            continue;
        }

        {
            lock_guard<mutex> lock(frameMutex_);
            latestFrame_ = frame;
            ++latestFrameId_;
            hasFrame_ = true;
        }

        if (sourceType_ == StreamSourceType::VideoFile)
        {
            nextVideoFrameTime += videoFrameInterval;
            const auto now = chrono::steady_clock::now();
            if (nextVideoFrameTime > now)
                this_thread::sleep_until(nextVideoFrameTime);
            else if (now - nextVideoFrameTime > videoFrameInterval * 5)
                nextVideoFrameTime = now;
        }
    }

    opened_ = false;
    if (cap_.isOpened()) cap_.release();
}