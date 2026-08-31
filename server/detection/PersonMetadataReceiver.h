#pragma once

// 한화비전 RTSP 프로필의 ONVIF 분석 XML 데이터 트랙에서 사람 박스를 읽는다.
// FFmpeg는 영상을 다시 디코딩하지 않고 메타데이터 패킷만 복사한다.


#include <memory>
#include <string>

#include <opencv2/opencv.hpp>

#include "DetectionTypes.h"

class PersonMetadataReceiver
{
public:
    PersonMetadataReceiver();
    ~PersonMetadataReceiver();

    PersonMetadataReceiver(const PersonMetadataReceiver&) = delete;
    PersonMetadataReceiver& operator=(const PersonMetadataReceiver&) = delete;

    bool start(const std::string& rtspUrl);
    void stop();
    // 최신 정규화 좌표를 현재 영상 크기의 픽셀 좌표로 변환해 반환한다.
    PersonMetadataFrame snapshot(const cv::Size& videoFrameSize) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
