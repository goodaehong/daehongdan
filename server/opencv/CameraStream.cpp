#include "CameraStream.h"

#include <chrono>
#include <iostream>
#include <utility>
#include <vector>

using namespace cv;
using namespace std;

CameraStream::CameraStream(
    const string& sourceValue,
    StreamSourceType sourceTypeValue,
    bool loopVideoFileValue
)
    : source(sourceValue),
    sourceType(sourceTypeValue),
    loopVideoFile(loopVideoFileValue)
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

    // 이전 파일 재생 스레드가 정상 종료된 뒤 다시 start()가 호출된 경우 정리한다.
    if (readerThread.joinable())
        readerThread.join();

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

    // latestFrame은 읽기 스레드에서 매 프레임 새 Mat으로 교체되므로
    // 여기서는 참조 카운팅을 이용해 불필요한 clone을 피한다.
    outFrame = latestFrame;
    lastFrameId = latestFrameId;

    return true;
}

bool CameraStream::openSource()
{
    if (cap.isOpened())
        cap.release();

    if (sourceType == StreamSourceType::VideoFile)
    {
        cout << "Opening video file: " << source << endl;

        // FFmpeg를 먼저 시도하고, 실패하면 사용 가능한 다른 백엔드를 시도한다.
        if (!cap.open(source, CAP_FFMPEG))
        {
            cap.release();

            if (!cap.open(source, CAP_ANY))
            {
                cout << "Video file open failed: " << source << endl;
                return false;
            }
        }

        opened = true;

        cout
            << "Video file opened"
            << " | "
            << static_cast<int>(cap.get(CAP_PROP_FRAME_WIDTH))
            << "x"
            << static_cast<int>(cap.get(CAP_PROP_FRAME_HEIGHT))
            << " | FPS "
            << cap.get(CAP_PROP_FPS)
            << endl;

        return true;
    }

    const vector<int> openParams =
    {
        CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
        CAP_PROP_READ_TIMEOUT_MSEC, 2000
    };

    cout << "Camera connecting..." << endl;

    if (!cap.open(source, CAP_FFMPEG, openParams))
    {
        cout << "Camera open failed: " << source << endl;
        return false;
    }

    if (!cap.set(CAP_PROP_BUFFERSIZE, 1))
    {
        cout
            << "Camera buffer size option is not supported by this backend"
            << endl;
    }

    opened = true;
    cout << "Camera connected" << endl;

    return true;
}

void CameraStream::readLoop()
{
    chrono::steady_clock::time_point nextVideoFrameTime;
    chrono::steady_clock::duration videoFrameInterval =
        chrono::milliseconds(33);

    while (running.load())
    {
        if (!cap.isOpened())
        {
            if (!openSource())
            {
                opened = false;

                this_thread::sleep_for(
                    chrono::milliseconds(500)
                );

                continue;
            }

            if (sourceType == StreamSourceType::VideoFile)
            {
                double fps = cap.get(CAP_PROP_FPS);

                if (fps <= 1.0 || fps > 240.0)
                    fps = 30.0;

                videoFrameInterval =
                    chrono::duration_cast<chrono::steady_clock::duration>(
                        chrono::duration<double>(1.0 / fps)
                    );

                nextVideoFrameTime =
                    chrono::steady_clock::now();
            }
        }

        Mat frame;

        if (!cap.read(frame) || frame.empty())
        {
            if (sourceType == StreamSourceType::VideoFile)
            {
                if (loopVideoFile)
                {
                    cout << "Video finished. Restarting from beginning." << endl;

                    cap.set(CAP_PROP_POS_FRAMES, 0);
                    nextVideoFrameTime = chrono::steady_clock::now();

                    // 일부 코덱은 POS_FRAMES 이동 직후 한 번 더 실패할 수 있으므로
                    // 다음 반복에서 다시 read한다.
                    continue;
                }

                cout << "Video playback finished." << endl;

                running = false;
                opened = false;
                break;
            }

            opened = false;
            cout << "Camera read failed. Reconnecting..." << endl;

            cap.release();

            this_thread::sleep_for(
                chrono::milliseconds(200)
            );

            continue;
        }

        Mat ownedFrame = frame.clone();

        {
            lock_guard<mutex> lock(frameMutex);
            latestFrame = std::move(ownedFrame);
            ++latestFrameId;
            hasFrame = true;
        }

        // 파일은 가능한 최대 속도로 읽으면 영상 대부분을 건너뛰게 된다.
        // 원본 FPS 간격에 맞춰 실제 재생 속도로 공급한다.
        if (sourceType == StreamSourceType::VideoFile)
        {
            nextVideoFrameTime += videoFrameInterval;

            const auto now = chrono::steady_clock::now();

            if (nextVideoFrameTime > now)
            {
                this_thread::sleep_until(nextVideoFrameTime);
            }
            else if (now - nextVideoFrameTime >
                videoFrameInterval * 5)
            {
                // 처리 지연이 크게 누적되면 기준 시각을 현재로 재설정한다.
                nextVideoFrameTime = now;
            }
        }
    }

    opened = false;

    if (cap.isOpened())
        cap.release();
}