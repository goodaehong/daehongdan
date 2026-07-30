#pragma once

#include <memory>
#include <string>

#include <opencv2/opencv.hpp>

#include "DetectionTypes.h"

// Reads the ONVIF analytics XML data track from a Hanwha RTSP profile.
// FFmpeg only copies metadata packets; it does not decode the video again.
class PersonMetadataReceiver
{
public:
    PersonMetadataReceiver();
    ~PersonMetadataReceiver();

    PersonMetadataReceiver(const PersonMetadataReceiver&) = delete;
    PersonMetadataReceiver& operator=(const PersonMetadataReceiver&) = delete;

    bool start(const std::string& rtspUrl);
    void stop();
    PersonMetadataFrame snapshot(const cv::Size& videoFrameSize) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
