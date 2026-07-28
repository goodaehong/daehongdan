#include "ConsoleFireApplication.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

#include "AppConfig.h"
#include "CameraStream.h"
#include "FireDetectionRuntime.h"
#include "FireView.h"
#include "SmokeDetectionRuntime.h"

using namespace cv;
using namespace std;

namespace
{
    struct InputSelection
    {
        string source;
        string displayName;
        StreamSourceType type = StreamSourceType::RtspCamera;
        bool loop = false;
    };

    struct ChannelContext
    {
        ChannelContext(std::size_t channelIndex, const InputSelection& selection)
            : index(channelIndex),
              input(selection),
              camera(new CameraStream(selection.source, selection.type, selection.loop)),
              fireRuntime(new FireDetectionRuntime())
        {
        }

        std::size_t index = 0;
        InputSelection input;
        unique_ptr<CameraStream> camera;
        unique_ptr<FireDetectionRuntime> fireRuntime;
        Mat latestDisplayFrame;
        FireRuntimeSnapshot latestFireSnapshot;
        SmokeRuntimeSnapshot latestSmokeSnapshot;
        uint64_t lastFrameId = 0;
        uint64_t lastSourceGeneration = 0;
        bool streamWasOpen = false;
        chrono::steady_clock::time_point previousDisplayTime = chrono::steady_clock::now();
        double averageDisplayFps = 0.0;
        bool reportInitialized = false;
        bool lastReportedFire = false;
        bool lastReportedSmoke = false;
    };

    void configureRtspBackend()
    {
#if RTSP_USE_UDP
        const char* options =
            "rtsp_transport;udp|fflags;nobuffer|flags;low_delay|max_delay;0|analyzeduration;0|probesize;2048";
#else
        const char* options =
            "rtsp_transport;tcp|fflags;nobuffer|flags;low_delay|max_delay;100000|analyzeduration;0|probesize;4096";
#endif
#ifdef _WIN32
        _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS", options);
#else
        setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS", options, 1);
#endif
    }

    string makeRtspSource(const string& cameraIp, int channelIndex)
    {
        return string("rtsp://") + RTSP_USERNAME + ':' + RTSP_PASSWORD + '@' +
            cameraIp + ":554/" + to_string(channelIndex) + RTSP_PROFILE_SUFFIX;
    }

    vector<InputSelection> selectInputs()
    {
        vector<InputSelection> inputs;
#if USE_VIDEO_FILE
        InputSelection input;
        input.source = VIDEO_FILE_PATH;
        input.displayName = "VIDEO";
        input.type = StreamSourceType::VideoFile;
        input.loop = VIDEO_FILE_LOOP != 0;
        cout << "Input mode: VIDEO FILE\nVideo path: " << input.source << endl;
        inputs.push_back(std::move(input));
#else
        configureRtspBackend();
        cout << "Input mode: RTSP CAMERA (up to "
            << smoke_config::MAX_CHANNELS << " channels)" << endl;
        string cameraIp;
        while (cameraIp.empty())
        {
            cout << "Camera IP : ";
            getline(cin, cameraIp);
            if (cameraIp.empty()) cerr << "Camera IP is required." << endl;
        }

        for (int index = 0; index < smoke_config::MAX_CHANNELS; ++index)
        {
            InputSelection input;
            input.source = makeRtspSource(cameraIp, index);
            input.displayName = "CH" + to_string(index + 1);
            input.type = StreamSourceType::RtspCamera;
            inputs.push_back(std::move(input));
        }
#endif
        return inputs;
    }

    void reportStateChange(
        ChannelContext& channel,
        const FireRuntimeSnapshot& fire,
        const SmokeRuntimeSnapshot& smoke)
    {
        const bool fireDetected = fire.alarm.alarmActive;
        const bool smokeDetected = smoke.smokeDetected;
        if (channel.reportInitialized &&
            fireDetected == channel.lastReportedFire &&
            smokeDetected == channel.lastReportedSmoke)
            return;

        channel.reportInitialized = true;
        channel.lastReportedFire = fireDetected;
        channel.lastReportedSmoke = smokeDetected;

        // This is the same data the Raspberry Pi server should serialize to Qt.
        cout << channel.input.displayName
            << " | fire=" << (fireDetected ? 1 : 0)
            << " | smoke=" << (smokeDetected ? 1 : 0)
            << " | smokeScore=" << smoke.smokeScore
            << " | smokeFrame=" << smoke.resultFrameId
            << endl;
    }
}

int ConsoleFireApplication::run()
{
    const vector<InputSelection> inputs = selectInputs();
    if (inputs.empty())
    {
        cerr << "No camera channel is configured." << endl;
        return -1;
    }

    vector<unique_ptr<ChannelContext>> channels;
    channels.reserve(inputs.size());
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        unique_ptr<ChannelContext> channel(new ChannelContext(index, inputs[index]));
        if (!channel->camera->start())
        {
            cerr << channel->input.displayName << ": input stream thread start failed" << endl;
            return -1;
        }
        channels.push_back(std::move(channel));
    }

    SmokeDetectionRuntime smokeRuntime(
        channels.size(),
        smoke_config::MODEL_PARAM_PATH,
        smoke_config::MODEL_BIN_PATH);
    if (!smokeRuntime.isModelReady())
        cerr << "Smoke model unavailable: " << smokeRuntime.modelError() << endl;
    else
        cout << "Smoke NCNN loaded once for " << channels.size() << " channel(s)" << endl;

    FireView gridView("Fire/Smoke 4-Channel");
    bool keepRunning = true;
    while (keepRunning)
    {
        bool receivedAnyFrame = false;
        bool everyVideoFinished = true;

        for (unique_ptr<ChannelContext>& channelPtr : channels)
        {
            ChannelContext& channel = *channelPtr;
            Mat frame;
            if (!channel.camera->getLatestFrame(frame, channel.lastFrameId))
            {
                if (!channel.camera->isOpened() && channel.streamWasOpen)
                {
                    channel.streamWasOpen = false;
                    channel.fireRuntime->resetStream();
                    smokeRuntime.resetChannel(channel.index);
                    channel.latestDisplayFrame.release();
                    channel.latestFireSnapshot = FireRuntimeSnapshot{};
                    channel.latestSmokeSnapshot = SmokeRuntimeSnapshot{};
                }
                if (channel.input.type != StreamSourceType::VideoFile ||
                    channel.camera->isRunning())
                    everyVideoFinished = false;
                continue;
            }
            if (frame.empty()) continue;

            receivedAnyFrame = true;
            everyVideoFinished = false;
            const uint64_t sourceGeneration = channel.camera->sourceGeneration();
            if (!channel.streamWasOpen ||
                sourceGeneration != channel.lastSourceGeneration)
            {
                channel.streamWasOpen = true;
                channel.lastSourceGeneration = sourceGeneration;
                channel.fireRuntime->resetStream();
                smokeRuntime.resetChannel(channel.index);
            }

            const auto now = chrono::steady_clock::now();
            channel.fireRuntime->submitFrame(frame, channel.lastFrameId, now);
            smokeRuntime.submitFrame(channel.index, frame, channel.lastFrameId, now);

            const FireRuntimeSnapshot fireSnapshot = channel.fireRuntime->poll(now);
            const SmokeRuntimeSnapshot smokeSnapshot = smokeRuntime.poll(channel.index, now);
            frame.copyTo(channel.latestDisplayFrame);
            channel.latestFireSnapshot = fireSnapshot;
            channel.latestSmokeSnapshot = smokeSnapshot;
            reportStateChange(channel, fireSnapshot, smokeSnapshot);

            const double interval =
                chrono::duration<double>(now - channel.previousDisplayTime).count();
            channel.previousDisplayTime = now;
            const double currentFps = interval > 0.0 ? 1.0 / interval : 0.0;
            channel.averageDisplayFps = channel.averageDisplayFps <= 0.0
                ? currentFps
                : channel.averageDisplayFps * 0.90 + currentFps * 0.10;

        }

        vector<FireViewChannel> viewChannels;
        viewChannels.reserve(channels.size());
        for (const unique_ptr<ChannelContext>& channel : channels)
        {
            FireViewChannel viewChannel;
            viewChannel.frame = channel->latestDisplayFrame;
            viewChannel.fire = channel->latestFireSnapshot;
            viewChannel.smoke = channel->latestSmokeSnapshot;
            viewChannel.displayFps = channel->averageDisplayFps;
            viewChannel.title = channel->input.displayName;
            viewChannels.push_back(std::move(viewChannel));
        }
        if (!gridView.showGrid(viewChannels)) break;

#if USE_VIDEO_FILE && !VIDEO_FILE_LOOP
        if (everyVideoFinished) break;
#else
        (void)everyVideoFinished;
#endif

        if (!receivedAnyFrame)
        {
            this_thread::sleep_for(chrono::milliseconds(1));
        }
    }

    smokeRuntime.stop();
    for (unique_ptr<ChannelContext>& channel : channels)
    {
        channel->fireRuntime->stop();
        channel->camera->stop();
    }
    gridView.close();
    return 0;
}
