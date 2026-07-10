#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────
// 설정 (바뀌면 여기만 수정)
// ─────────────────────────────────────────────────────────
const std::string CAM_IP   = "172.20.35.178";   // ★ IP 바뀌면 이 줄만 고치면 됨
const std::string CAM_USER = "admin";
const std::string CAM_PW   = "5hanwha!";
const int NUM_CH = 4;

// 각 채널을 파이프라인에서 이 크기로 줄여서 받음 (성능 + 창 크기)
const int CW = 640, CH = 360;

// ─────────────────────────────────────────────────────────
// 채널별 GStreamer 파이프라인 문자열 생성
//   - 방식1: 끝을 appsink 로 두어 프레임을 OpenCV로 전달
//   - videoscale 로 파이프라인 단계에서 미리 축소 (가벼움)
//   - avdec_h264 = CPU 디코더 (라즈베리파이 이식 시 v4l2h264dec 로 교체)
// ─────────────────────────────────────────────────────────
std::string makePipeline(int ch) {
    return
        "rtspsrc location=rtsp://" + CAM_USER + ":" + CAM_PW + "@" + CAM_IP +
        ":554/" + std::to_string(ch) + "/profile2/media.smp latency=0 "
        "! rtph264depay ! h264parse ! avdec_h264 "
        "! videoconvert ! videoscale "
        "! video/x-raw,format=BGR,width=" + std::to_string(CW) +
        ",height=" + std::to_string(CH) + " "
        "! appsink drop=true max-buffers=1";
}

// 공용 저장소
std::vector<cv::Mat> g_frames(NUM_CH);
std::mutex g_mutex;
std::atomic<bool> g_stop(false);

// ─────────────────────────────────────────────────────────
// 스레드: 자기 채널 프레임 수신 → 저장소에 저장 (그리기는 안 함)
// ─────────────────────────────────────────────────────────
void captureWorker(int idx) {
    std::string pipeline = makePipeline(idx);
    cv::VideoCapture cap(pipeline, cv::CAP_GSTREAMER);

    if (!cap.isOpened()) {
        std::cerr << "[ch" << idx << "] 파이프라인 열기 실패\n";
        return;
    }
    std::cout << "[ch" << idx << "] 연결 성공\n";

    cv::Mat frame;
    while (!g_stop.load()) {
        if (!cap.read(frame) || frame.empty()) {
            std::cerr << "[ch" << idx << "] 프레임 끊김\n";
            break;
        }
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_frames[idx] = frame.clone();
        }
    }
    cap.release();
    std::cout << "[ch" << idx << "] 수신 종료\n";
}

int main() {
    // 스레드 4개 시작
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_CH; i++)
        threads.emplace_back(captureWorker, i);

    std::cout << "4채널 수신 시작. 영상 창에서 ESC 종료.\n";

    // 창을 리사이즈 가능하게 생성 (마우스로 크기 조절 됨)
    cv::namedWindow("4CH Grid", cv::WINDOW_NORMAL);
    cv::resizeWindow("4CH Grid", CW * 2, CH * 2);   // 초기 크기 지정

    // ─────────────────────────────────────────────────────
    // 메인: 저장소에서 4장 취득 → 2x2 병합 → 표시
    // ─────────────────────────────────────────────────────
    while (!g_stop.load()) {
        cv::Mat grid(CH * 2, CW * 2, CV_8UC3, cv::Scalar(0, 0, 0));

        std::vector<cv::Mat> local(NUM_CH);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (int i = 0; i < NUM_CH; i++)
                if (!g_frames[i].empty())
                    local[i] = g_frames[i].clone();
        }

        for (int i = 0; i < NUM_CH; i++) {
            if (local[i].empty()) continue;
            // 파이프라인에서 이미 CWxCH로 줄여서 오지만, 안전하게 한 번 더 맞춤
            cv::Mat cell;
            if (local[i].cols != CW || local[i].rows != CH)
                cv::resize(local[i], cell, cv::Size(CW, CH));
            else
                cell = local[i];

            int row = i / 2, col = i % 2;
            cell.copyTo(grid(cv::Rect(col * CW, row * CH, CW, CH)));
        }

        cv::imshow("4CH Grid", grid);
        if (cv::waitKey(1) == 27) g_stop.store(true);
    }

    for (auto& t : threads)
        if (t.joinable()) t.join();
    cv::destroyAllWindows();
    std::cout << "전체 종료.\n";
    return 0;
}
