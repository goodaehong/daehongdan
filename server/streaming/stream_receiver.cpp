#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "FireDetectionRuntime.h"

// 4채널 프레임 공유 저장소. 채널별 mutex로 워커/메인 간 경합 방지
struct FrameStore {
    cv::Mat frames[4];
    std::mutex mtx[4];
};

// 감지 JSON을 TCP로 내보내는 공용 sender. 4채널 워커가 소켓 1개를 공유 → write를 mutex로 보호
class Sender {
public:
    bool connect(const std::string& host, int port) {
        std::lock_guard<std::mutex> lock(mtx_);
        host_ = host;
        port_ = port;
        return connectLocked();
    }

    void send(const std::string& line) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (sockfd_ < 0 && !connectLocked()) return;  // 미연결이면 재연결, 실패 시 이번 메시지 버림

        std::string buf = line + "\n";                 // \n = 메시지 경계
        ssize_t n = ::send(sockfd_, buf.data(), buf.size(), MSG_NOSIGNAL);
        if (n <= 0) {                                  // 상대 끊김 → 닫고 다음번 재연결
            ::close(sockfd_);
            sockfd_ = -1;
        }
    }

    ~Sender() {
        if (sockfd_ >= 0) ::close(sockfd_);
    }

private:
    bool connectLocked() {   // mtx_ 잠근 상태에서만 호출
        sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
            ::close(sockfd_); sockfd_ = -1; return false;
        }
        if (::connect(sockfd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            ::close(sockfd_); sockfd_ = -1; return false;
        }
        return true;
    }

    int sockfd_ = -1;
    std::mutex mtx_;
    std::string host_;
    int port_ = 0;
};

// 채널 1개 담당. RTSP 연결 → 프레임 읽기 → 감지 → JSON 전송. 끊기면 재연결
void worker(int ch, FrameStore& store, Sender& sender) {
    std::string url = "rtsp://localhost:8554/cam" + std::to_string(ch + 1);

    FireDetectionRuntime runtime;   // 복사 금지 타입 → 채널당 지역변수 1개
    std::uint64_t frameId = 0;

    while (true) {
        cv::VideoCapture cap;
        cap.open(url, cv::CAP_FFMPEG);

        if (!cap.isOpened()) {
            std::cerr << "[cam" << ch + 1 << "] 연결 실패, 3초 후 재시도\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        std::cout << "[cam" << ch + 1 << "] 연결 성공\n";
        runtime.resetStream();   // 재연결이면 이전 추적 상태 폐기

        cv::Mat frame;
        while (true) {
            if (!cap.read(frame) || frame.empty()) {
                std::cerr << "[cam" << ch + 1 << "] 프레임 읽기 실패, 재연결\n";
                break;
            }
            {
                std::lock_guard<std::mutex> lock(store.mtx[ch]);
                store.frames[ch] = frame.clone();
            }

            runtime.submitFrame(frame, frameId++);
            FireRuntimeSnapshot snap = runtime.poll();

            if (snap.boxIsFresh) {
                // 계약① JSON 조립 (박스 0개여도 전송 → Qt가 오버레이 지움)
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2);
                oss << "{\"type\":\"detection\""
                    << ",\"channel\":" << (ch + 1)
                    << ",\"frameId\":" << snap.resultFrameId
                    << ",\"srcW\":" << frame.cols
                    << ",\"srcH\":" << frame.rows
                    << ",\"alarm\":" << (snap.alarm.alarmActive ? "true" : "false")
                    << ",\"boxes\":[";

                for (size_t i = 0; i < snap.detection.boxes.size(); ++i) {
                    const auto& b = snap.detection.boxes[i];
                    if (i > 0) oss << ",";
                    oss << "{\"x\":" << b.box.x
                        << ",\"y\":" << b.box.y
                        << ",\"w\":" << b.box.width
                        << ",\"h\":" << b.box.height
                        << ",\"cls\":\"" << (b.type == DetectionType::FIRE ? "FIRE" : "SMOKE") << "\""
                        << ",\"score\":" << b.score
                        << "}";
                }
                oss << "]}";

                sender.send(oss.str());
            }
        }

        cap.release();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

int main() {
    Sender sender;
    sender.connect("127.0.0.1", 9999);   // mock 테스트 포트. 나중에 광렬 TLS 서버 포트로 교체

    FrameStore store;
    std::thread threads[4];

    for (int i = 0; i < 4; i++) {
        threads[i] = std::thread(worker, i, std::ref(store), std::ref(sender));
    }

    for (int i = 0; i < 4; i++) {
        threads[i].join();
    }

    return 0;
}