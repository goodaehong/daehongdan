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
#include "PersonMetadataReceiver.h"
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
              fireRuntime(new FireDetectionRuntime()),
              personReceiver(new PersonMetadataReceiver())
        {
        }

        std::size_t index = 0;
        InputSelection input;
        unique_ptr<CameraStream> camera;
        unique_ptr<FireDetectionRuntime> fireRuntime;
        unique_ptr<PersonMetadataReceiver> personReceiver;
        Mat latestDisplayFrame;
        FireRuntimeSnapshot latestFireSnapshot;
        SmokeRuntimeSnapshot latestSmokeSnapshot;
        PersonMetadataFrame latestPersonMetadata;
        uint64_t lastFrameId = 0;
        uint64_t lastSourceGeneration = 0;
        bool streamWasOpen = false;
        chrono::steady_clock::time_point previousDisplayTime = chrono::steady_clock::now();
        double averageDisplayFps = 0.0;
        bool reportInitialized = false;
        bool lastReportedFire = false;
        bool lastReportedSmoke = false;
        chrono::steady_clock::time_point lastPersonReport =
            chrono::steady_clock::time_point::min();
        bool personReportInitialized = false;
        bool lastPersonConnected = false;
        size_t lastPersonCount = 0;
        string lastPersonStatus;
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

    void reportPersonBoxes(
        ChannelContext& channel,
        const PersonMetadataFrame& metadata,
        chrono::steady_clock::time_point now)
    {
        const bool stateChanged = !channel.personReportInitialized ||
            metadata.streamConnected != channel.lastPersonConnected ||
            metadata.persons.size() != channel.lastPersonCount ||
            metadata.status != channel.lastPersonStatus;
        const bool intervalElapsed =
            channel.lastPersonReport == chrono::steady_clock::time_point::min() ||
            chrono::duration_cast<chrono::milliseconds>(
                now - channel.lastPersonReport).count() >=
            person_metadata_config::REPORT_INTERVAL_MS;

        // Moving person coordinates are emitted at a controlled rate. Empty,
        // unchanged frames are silent so four channels do not flood stdout.
        if (!stateChanged && (!intervalElapsed || metadata.persons.empty())) return;

        channel.personReportInitialized = true;
        channel.lastPersonConnected = metadata.streamConnected;
        channel.lastPersonCount = metadata.persons.size();
        channel.lastPersonStatus = metadata.status;
        channel.lastPersonReport = now;

        cout << channel.input.displayName
            << " | personMeta=" << (metadata.streamConnected ? 1 : 0)
            << " | persons=" << metadata.persons.size()
            << " | boxes=[";
        for (size_t index = 0; index < metadata.persons.size(); ++index)
        {
            const PersonDetection& person = metadata.persons[index];
            if (index) cout << ';';
            cout << "{x:" << person.box.x
                << ",y:" << person.box.y
                << ",w:" << person.box.width
                << ",h:" << person.box.height
                << ",score:" << person.confidence;
            if (!person.objectId.empty())
                cout << ",id:" << person.objectId;
            cout << '}';
        }
        cout << ']';
        if (!metadata.streamConnected && !metadata.status.empty())
            cout << " | status=" << metadata.status;
        cout << endl;
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
        if (person_metadata_config::ENABLED &&
            channel->input.type == StreamSourceType::RtspCamera)
        {
            const bool started = channel->personReceiver->start(channel->input.source);
            cout << channel->input.displayName << ": WiseAI person metadata "
                << (started ? "receiver started" : "receiver start failed") << endl;
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
            const PersonMetadataFrame personMetadata =
                channel.personReceiver->snapshot(frame.size());
            frame.copyTo(channel.latestDisplayFrame);
            channel.latestFireSnapshot = fireSnapshot;
            channel.latestSmokeSnapshot = smokeSnapshot;
            channel.latestPersonMetadata = personMetadata;
            reportStateChange(channel, fireSnapshot, smokeSnapshot);
            reportPersonBoxes(channel, personMetadata, now);

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
            viewChannel.person = channel->latestPersonMetadata;
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
        channel->personReceiver->stop();
        channel->fireRuntime->stop();
        channel->camera->stop();
    }
    gridView.close();
    return 0;
}
