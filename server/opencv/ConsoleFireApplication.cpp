#include "ConsoleFireApplication.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <opencv2/opencv.hpp>

#include "AppConfig.h"
#include "CameraStream.h"
#include "FireDetectionRuntime.h"
#include "FireView.h"

using namespace cv;
using namespace std;

namespace
{
struct InputSelection
{
    string source;
    StreamSourceType type = StreamSourceType::RtspCamera;
    bool loop = false;
};

void configureRtspBackend()
{
#if RTSP_USE_UDP
    const char* options = "rtsp_transport;udp|fflags;nobuffer|flags;low_delay|max_delay;0|analyzeduration;0|probesize;2048";
#else
    const char* options = "rtsp_transport;tcp|fflags;nobuffer|flags;low_delay|max_delay;100000|analyzeduration;0|probesize;4096";
#endif
#ifdef _WIN32
    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", options);
#else
    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", options, 1);
#endif
}

InputSelection selectInput()
{
    InputSelection input;
#if USE_VIDEO_FILE
    input.source = VIDEO_FILE_PATH;
    input.type = StreamSourceType::VideoFile;
    input.loop = VIDEO_FILE_LOOP != 0;
    cout << "Input mode: VIDEO FILE\nVideo path: " << input.source << endl;
#else
    configureRtspBackend();
    string cameraIp;
    cout << "Camera IP : ";
    cin >> cameraIp;
    input.source = string("rtsp://") + RTSP_USERNAME + ':' + RTSP_PASSWORD + '@' +
        cameraIp + ":554" + RTSP_PROFILE_PATH;
    input.type = StreamSourceType::RtspCamera;
    cout << "Input mode: RTSP CAMERA" << endl;
#endif
    return input;
}
}

int ConsoleFireApplication::run()
{
    const InputSelection input = selectInput();
    CameraStream camera(input.source, input.type, input.loop);
    if (!camera.start())
    {
        cerr << "Input stream thread start failed" << endl;
        return -1;
    }

    FireDetectionRuntime runtime;
    FireView view;
    uint64_t lastFrameId = 0;
    bool streamWasOpen = false;
    auto previousDisplayTime = chrono::steady_clock::now();
    double averageDisplayFps = 0.0;

    while (true)
    {
        Mat frame;
        if (!camera.getLatestFrame(frame, lastFrameId))
        {
            if (!camera.isOpened() && streamWasOpen)
            {
                streamWasOpen = false;
                runtime.resetStream();
            }
            if (!view.processEvents(1)) break;
            this_thread::sleep_for(chrono::milliseconds(1));
            continue;
        }
        if (frame.empty()) continue;

        if (!streamWasOpen)
        {
            streamWasOpen = true;
            runtime.resetStream();
        }

        const auto now = chrono::steady_clock::now();
        runtime.submitFrame(frame, lastFrameId, now);
        FireRuntimeSnapshot snapshot = runtime.poll(now);

        const double interval = chrono::duration<double>(now - previousDisplayTime).count();
        previousDisplayTime = now;
        const double currentFps = interval > 0.0 ? 1.0 / interval : 0.0;
        averageDisplayFps = averageDisplayFps <= 0.0 ? currentFps :
            averageDisplayFps * 0.90 + currentFps * 0.10;

        if (!view.show(frame, snapshot, averageDisplayFps)) break;
    }

    runtime.stop();
    camera.stop();
    view.close();
    return 0;
}
