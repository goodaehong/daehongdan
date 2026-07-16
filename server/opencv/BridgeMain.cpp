// Qt 대시보드로 화재/연기 감지 오버레이 영상을 TCP로 스트리밍하는 브릿지 서버.
// 프로토콜(클라이언트 접속당 반복): [4바이트 빅엔디안 JPEG 길이][JPEG 바이트][1바이트 알람 플래그(0/1)]
#include "AppConfig.h"
#include "CameraStream.h"
#include "FireDetectionRuntime.h"

#include <opencv2/opencv.hpp>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {

void drawOverlay(cv::Mat &frame, const FireRuntimeSnapshot &snapshot)
{
    for (const auto &box : snapshot.detection.boxes) {
        const cv::Scalar color = box.type == DetectionType::FIRE ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 165, 255);
        cv::rectangle(frame, box.box, color, 2);
        cv::putText(frame, box.label, box.box.tl() + cv::Point(0, -6),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
    }
    if (snapshot.alarm.alarmActive) {
        cv::rectangle(frame, cv::Rect(0, 0, frame.cols, frame.rows), cv::Scalar(0, 0, 255), 8);
        cv::putText(frame, "FIRE ALARM", cv::Point(20, 40),
                    cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 0, 255), 3);
    }
}

bool sendAll(SOCKET s, const char *data, int len)
{
    int sent = 0;
    while (sent < len) {
        const int n = send(s, data + sent, len - sent, 0);
        if (n <= 0)
            return false;
        sent += n;
    }
    return true;
}

}

int main(int argc, char **argv)
{
    const std::string cameraIp = argc > 1 ? argv[1] : "172.20.35.13";
    const int port = argc > 2 ? std::stoi(argv[2]) : 9100;

    _putenv_s("OPENCV_FFMPEG_CAPTURE_OPTIONS",
              "rtsp_transport;tcp|fflags;nobuffer|flags;low_delay|max_delay;100000|analyzeduration;0|probesize;4096");

    const std::string url = std::string("rtsp://") + RTSP_USERNAME + ':' + RTSP_PASSWORD + '@' +
        cameraIp + ":554" + RTSP_PROFILE_PATH;

    std::cout << "Connecting to camera: " << url << std::endl;
    CameraStream camera(url, StreamSourceType::RtspCamera, false);
    if (!camera.start()) {
        std::cerr << "Camera stream failed to start" << std::endl;
        return 1;
    }

    FireDetectionRuntime runtime;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<u_short>(port));
    bind(listenSock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    listen(listenSock, 1);
    std::cout << "Listening on port " << port << ", waiting for Qt client..." << std::endl;

    SOCKET client = accept(listenSock, nullptr, nullptr);
    std::cout << "Qt client connected. Streaming..." << std::endl;

    std::uint64_t lastFrameId = 0;
    bool streamWasOpen = false;

    while (true) {
        cv::Mat frame;
        if (!camera.getLatestFrame(frame, lastFrameId)) {
            if (!camera.isOpened() && streamWasOpen) {
                streamWasOpen = false;
                runtime.resetStream();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (frame.empty())
            continue;
        if (!streamWasOpen) {
            streamWasOpen = true;
            runtime.resetStream();
        }

        const auto now = std::chrono::steady_clock::now();
        runtime.submitFrame(frame, lastFrameId, now);
        const FireRuntimeSnapshot snapshot = runtime.poll(now);

        drawOverlay(frame, snapshot);

        std::vector<uchar> jpegBuf;
        cv::imencode(".jpg", frame, jpegBuf, { cv::IMWRITE_JPEG_QUALITY, 80 });

        const uint32_t len = static_cast<uint32_t>(jpegBuf.size());
        const uint32_t lenNet = htonl(len);
        const uint8_t alarmByte = snapshot.alarm.alarmActive ? 1 : 0;

        if (!sendAll(client, reinterpret_cast<const char *>(&lenNet), 4))
            break;
        if (!sendAll(client, reinterpret_cast<const char *>(jpegBuf.data()), static_cast<int>(jpegBuf.size())))
            break;
        if (!sendAll(client, reinterpret_cast<const char *>(&alarmByte), 1))
            break;
    }

    std::cout << "Client disconnected, shutting down." << std::endl;
    closesocket(client);
    closesocket(listenSock);
    WSACleanup();
    runtime.stop();
    camera.stop();
    return 0;
}
