#include "CameraStream.h"

#include <chrono>
#include <iostream>
#include <utility>
#include <vector>

using namespace cv;
using namespace std;

CameraStream::CameraStream(const string& rtspUrl)
    : url(rtspUrl)
{
}

CameraStream::~CameraStream()
{
    stop();
}

bool CameraStream::start()
{
    bool expected = false;

    // 이미 실행 중인 스레드를 다시 만들지 않는다.
    if (!running.compare_exchange_strong(expected, true))
        return false;

    {
        lock_guard<mutex> lock(frameMutex);
        latestFrame.release();
        latestFrameId = 0;
        hasFrame = false;
    }

    opened = false;
    readerThread = thread(&CameraStream::readLoop, this);

    return true;
}

void CameraStream::stop()
{
    running = false;

    if (readerThread.joinable())
        readerThread.join();

    if (cap.isOpened())
        cap.release();

    opened = false;
    hasFrame = false;
}

bool CameraStream::isOpened() const
{
    return opened.load();
}

bool CameraStream::getLatestFrame(
    Mat& outFrame,
    uint64_t& lastFrameId
)
{
    lock_guard<mutex> lock(frameMutex);

    if (!hasFrame.load() || latestFrame.empty())
        return false;

    // 이전 호출에서 이미 처리한 프레임이다.
    if (latestFrameId == lastFrameId)
        return false;

    // Mat은 참조 카운팅을 사용하므로 여기서는 clone하지 않는다.
    outFrame = latestFrame;
    lastFrameId = latestFrameId;

    return true;
}

void CameraStream::readLoop()
{
    while (running.load())
    {
        if (!cap.isOpened())
        {
            const vector<int> openParams =
            {
                CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
                CAP_PROP_READ_TIMEOUT_MSEC, 2000
            };

            cout << "Camera connecting..." << endl;

            if (!cap.open(url, CAP_FFMPEG, openParams))
            {
                opened = false;
                cout << "Camera open failed. Retry..." << endl;

                this_thread::sleep_for(chrono::milliseconds(500));
                continue;
            }

            cap.set(CAP_PROP_BUFFERSIZE, 1);
            opened = true;

            cout << "Camera connected" << endl;
        }

        Mat frame;

        // 시작 시 grab() 30회는 하지 않는다.
        if (!cap.read(frame) || frame.empty())
        {
            opened = false;
            cout << "Camera read failed. Reconnecting..." << endl;

            cap.release();
            this_thread::sleep_for(chrono::milliseconds(200));
            continue;
        }

        Mat ownedFrame = frame.clone();

        {
            lock_guard<mutex> lock(frameMutex);
            latestFrame = std::move(ownedFrame);
            ++latestFrameId;
            hasFrame = true;
        }
    }

    opened = false;

    if (cap.isOpened())
        cap.release();
}