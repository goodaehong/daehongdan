#include <opencv2/opencv.hpp>
#include <iostream>

int main() {
    // ─────────────────────────────────────────────────────────
    // GStreamer 파이프라인 문자열
    //   - 터미널에서 검증한 파이프라인과 동일한 구조
    //   - 차이점: 끝을 ximagesink(화면표시) → appsink(OpenCV로 전달)
    //   - 비번의 ! 는 %21 로 URL 인코딩 (코드 안전성)
    //   - appsink 앞에 caps로 BGR 지정: OpenCV cv::Mat 형식에 맞춤
    // ─────────────────────────────────────────────────────────
    std::string pipeline =
        "rtspsrc location=\"rtsp://admin:5hanwha!@172.20.35.178:554/0/profile2/media.smp\" latency=0 "
        "! rtph264depay ! h264parse ! avdec_h264 "
        "! videoconvert ! video/x-raw,format=BGR "
        "! appsink drop=true max-buffers=1";

    // ─────────────────────────────────────────────────────────
    // OpenCV에 GStreamer 파이프라인으로 연결
    //   두 번째 인자 cv::CAP_GSTREAMER 가 핵심
    // ─────────────────────────────────────────────────────────
    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);

    if (!cap.isOpened()) {
        std::cerr << "파이프라인 열기 실패!\n";
        std::cerr << "→ 파이프라인 문자열 또는 GStreamer 지원 확인 필요\n";
        return -1;
    }
    std::cout << "GStreamer 파이프라인 연결 성공! ESC 종료.\n";

    cv::Mat frame;
    while (true) {
        if (!cap.read(frame) || frame.empty()) {
            std::cerr << "프레임 못 받음.\n";
            break;
        }
        cv::imshow("GStreamer -> OpenCV (ch0)", frame);
        if (cv::waitKey(1) == 27) break;
    }

    cap.release();
    cv::destroyAllWindows();
    return 0;
}
