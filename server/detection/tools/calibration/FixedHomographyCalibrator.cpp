#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/core/utils/logger.hpp>
#include <opencv2/videoio.hpp>

#include "GridCoordinateMapper.h"

namespace
{
    struct Options
    {
        std::string source;
        std::string arucoConfiguration;
        std::string cameraCalibration;
        std::string output;
        int channel = 0;
        int acceptedUpdates = 30;
        std::uint64_t maximumFrames = 900;
    };

    void printUsage()
    {
        std::cout
            << "Usage: fixed_homography_calibrator <rtsp-or-video>"
            << " --channel <1..4> --aruco-config <file>"
            << " --camera-calibration <file> --output <file>"
            << " [--accepted-updates 30] [--max-frames 900]\n";
    }

    Options parseOptions(int argc, char** argv)
    {
        if (argc < 2)
        {
            printUsage();
            throw std::runtime_error("input source is required");
        }
        Options options;
        options.source = argv[1];
        for (int index = 2; index < argc; ++index)
        {
            const std::string argument = argv[index];
            auto requireValue = [&](const char* name) -> std::string
            {
                if (index + 1 >= argc)
                    throw std::runtime_error(std::string("missing value for ") + name);
                return argv[++index];
            };
            if (argument == "--channel")
                options.channel = std::stoi(requireValue("--channel"));
            else if (argument == "--aruco-config")
                options.arucoConfiguration = requireValue("--aruco-config");
            else if (argument == "--camera-calibration")
                options.cameraCalibration = requireValue("--camera-calibration");
            else if (argument == "--output")
                options.output = requireValue("--output");
            else if (argument == "--accepted-updates")
                options.acceptedUpdates = std::stoi(requireValue("--accepted-updates"));
            else if (argument == "--max-frames")
                options.maximumFrames = std::stoull(requireValue("--max-frames"));
            else if (argument == "--help" || argument == "-h")
            {
                printUsage();
                std::exit(0);
            }
            else
                throw std::runtime_error("unknown option: " + argument);
        }
        if (options.channel < 1 || options.channel > 4 ||
            options.arucoConfiguration.empty() ||
            options.cameraCalibration.empty() || options.output.empty() ||
            options.acceptedUpdates < 10 || options.maximumFrames < 1)
            throw std::runtime_error("invalid or incomplete options");
        return options;
    }

    bool isNetworkSource(const std::string& source)
    {
        std::string lower = source;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return lower.rfind("rtsp://", 0) == 0 || lower.rfind("rtsps://", 0) == 0;
    }

    cv::VideoCapture openCapture(const std::string& source)
    {
        cv::VideoCapture capture;
        if (isNetworkSource(source))
        {
            const std::vector<int> parameters{
                cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
                cv::CAP_PROP_READ_TIMEOUT_MSEC, 2500
            };
            capture.open(source, cv::CAP_FFMPEG, parameters);
            capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
        }
        else
            capture.open(source);
        return capture;
    }
}

int main(int argc, char** argv)
{
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);
    try
    {
        const Options options = parseOptions(argc, argv);
        GridCoordinateMapper mapper;
        const std::size_t channelIndex = static_cast<std::size_t>(options.channel - 1);
        if (!mapper.loadArucoBoardConfiguration(
            options.arucoConfiguration, channelIndex))
        {
            std::cerr << "ArUco configuration rejected: " << mapper.lastError() << '\n';
            return 2;
        }
        if (!mapper.loadCameraCalibration(options.cameraCalibration, channelIndex))
        {
            std::cerr << "Camera calibration rejected: "
                << mapper.cameraCalibrationError() << '\n';
            return 2;
        }

        cv::VideoCapture capture = openCapture(options.source);
        if (!capture.isOpened())
        {
            std::cerr << "Failed to open input source.\n";
            return 1;
        }

        int acceptedUpdates = 0;
        std::uint64_t frameId = 0;
        cv::Size frameSize;
        while (frameId < options.maximumFrames &&
            acceptedUpdates < options.acceptedUpdates)
        {
            cv::Mat frame;
            if (!capture.read(frame) || frame.empty())
            {
                std::cerr << "Frame read failed at frame " << frameId << ".\n";
                break;
            }
            ++frameId;
            if (frameSize.empty()) frameSize = frame.size();
            if (frame.size() != frameSize)
            {
                std::cerr << "Stream resolution changed during Homography calibration.\n";
                return 2;
            }

            if (mapper.updateFromFrame(frame, frameId))
            {
                ++acceptedUpdates;
                const ArucoMappingStatus status = mapper.status();
                std::cout << "accepted=" << acceptedUpdates << '/'
                    << options.acceptedUpdates << " frame=" << frameId
                    << " markers=" << status.acceptedMarkers << '/'
                    << status.detectedMarkers << " inlierCorners="
                    << status.inlierCorners << " rms="
                    << status.reprojectionRmsPx << "px\n";
            }
            else if (frameId == 1 || frameId % 30 == 0)
            {
                const ArucoMappingStatus status = mapper.status();
                std::cout << "waiting frame=" << frameId << " markers="
                    << status.acceptedMarkers << '/' << status.detectedMarkers
                    << " message=\"" << status.message << "\"\n";
            }
        }
        capture.release();

        if (acceptedUpdates < options.acceptedUpdates)
        {
            std::cerr << "Static Homography not saved: only " << acceptedUpdates
                << " accepted updates; required " << options.acceptedUpdates << ".\n";
            return 3;
        }
        if (!mapper.saveStaticHomography(options.output))
        {
            std::cerr << "Static Homography save failed: " << mapper.lastError() << '\n';
            return 4;
        }
        const ArucoMappingStatus status = mapper.status();
        std::cout << "Static Homography saved: channel=" << options.channel
            << " size=" << frameSize.width << 'x' << frameSize.height
            << " acceptedUpdates=" << acceptedUpdates << " finalRms="
            << status.reprojectionRmsPx << "px output=" << options.output << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    }
}
