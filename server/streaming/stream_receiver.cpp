#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <iostream>
#include <chrono>

// 4채널 프레임 공유 저장소. 채널별 mutex로 워커/메인 간 경합 방지
struct FrameStore {
    cv::Mat frames[4];
    std::mutex mtx[4];
};

// 채널 1개 담당. RTSP 연결 → 프레임 읽어서 저장소에 넣기. 끊기면 재연결
void worker(int ch, FrameStore& store) {
    std::string url = "rtsp://localhost:8554/cam" + std::to_string(ch + 1);

    while (true) {
        cv::VideoCapture cap;
        cap.open(url, cv::CAP_FFMPEG);

        if (!cap.isOpened()) {
            std::cerr << "[cam" << ch + 1 << "] 연결 실패, 3초 후 재시도\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        std::cout << "[cam" << ch + 1 << "] 연결 성공\n";

        cv::Mat frame;
        while (true) {
            // 읽기 실패/빈 프레임이면 내부 루프 탈출 → 바깥에서 재연결
            if (!cap.read(frame) || frame.empty()) {
                std::cerr << "[cam" << ch + 1 << "] 프레임 읽기 실패, 재연결\n";
                break;
            }
            // clone()으로 복사본 저장 (원본 frame은 다음 read에서 덮어써짐)
            std::lock_guard<std::mutex> lock(store.mtx[ch]);
            store.frames[ch] = frame.clone();
        }

        cap.release();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

int main() {
    // FFMPEG 저지연 옵션: TCP 전송 + 버퍼링 최소화. VideoCapture 생성 전에 설정해야 적용됨
    //setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS","rtsp_transport;tcp|fflags;nobuffer|flags;low_delay", 1);

    FrameStore store;
    std::thread threads[4];

    for (int i = 0; i < 4; i++) {
        threads[i] = std::thread(worker, i, std::ref(store));
    }

    
    for (int i = 0; i < 4; i++) {
        threads[i].join();
    }

    return 0;
}