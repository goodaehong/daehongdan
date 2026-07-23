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
#include <fcntl.h>
#include "FireDetectionRuntime.h"
#include <random>
#include <ctime>
#include <atomic>

// 4채널 프레임 공유 저장소. 채널별 mutex로 워커/메인 간 경합 방지
struct FrameStore {
    cv::Mat frames[4];
    std::mutex mtx[4];
};

// 채널별 최신 감지 상태. 워커가 갱신, 판단(센서 스레드)이 읽음
struct DetectionState {
    std::atomic<bool> fire{false};
    std::atomic<bool> smoke{false};
};
DetectionState detState[4];

// 감지 JSON을 TCP로 내보내는 공용 sender. connect(실서비스)/listen(테스트) 둘 다 지원.
class Sender {
public:
    // ── 실서비스용: 광렬 TLS 서버로 접속 (원래 있던 함수, 그대로 유지) ──
    bool connect(const std::string& host, int port) {
        std::lock_guard<std::mutex> lock(mtx_);
        mode_ = Mode::Client;                 // ★ 이 한 줄만 추가 (모드 표시)
        host_ = host;
        port_ = port;
        return connectLocked();
    }

    // ★★★ 여기부터 listen() 함수 통째로 새로 추가 ★★★
    // ── 테스트용: Qt(ServerLink)가 직접 접속하도록 서버가 listen ──
    bool listen(int port) {
        std::lock_guard<std::mutex> lock(mtx_);
        mode_ = Mode::Server;
        listenfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd_ < 0) return false;
        int opt = 1;
        ::setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET; addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (::bind(listenfd_, (sockaddr*)&addr, sizeof(addr)) < 0) { ::close(listenfd_); listenfd_ = -1; return false; }
        if (::listen(listenfd_, 1) < 0) { ::close(listenfd_); listenfd_ = -1; return false; }
        int fl = ::fcntl(listenfd_, F_GETFL, 0);
        ::fcntl(listenfd_, F_SETFL, fl | O_NONBLOCK);
        return true;
    }
    // ★★★ listen() 끝 ★★★

    // ── send(): 원래는 client 전용이었는데, 모드 분기하도록 내용 교체 ──
    void send(const std::string& line) {
        std::lock_guard<std::mutex> lock(mtx_);
        int fd;                                       // ★ 아래 if/else 블록 전체가 새 내용
        if (mode_ == Mode::Server) {                  // ★ 테스트(listen) 모드
            int newfd = ::accept(listenfd_, nullptr, nullptr);   // ★ 새 접속 있으면 받기
            if (newfd >= 0) {                                    // ★ 새 클라가 붙었으면
                if (clientfd_ >= 0) ::close(clientfd_);          // ★ 이전(죽은) 연결 닫고
                clientfd_ = newfd;                               // ★ 새 걸로 교체
            }
            if (clientfd_ < 0) return;                           // ★ 아직 아무도 안 붙음 → 버림
            fd = clientfd_;
        } else {                                      // ★ 실서비스(connect) 모드 = 원래 로직
            if (sockfd_ < 0 && !connectLocked()) return;
            fd = sockfd_;
        }
        std::string buf = line + "\n";                // (여기부터는 원래와 거의 같음)
        ssize_t n = ::send(fd, buf.data(), buf.size(), MSG_NOSIGNAL);
        if (n <= 0) {
            ::close(fd);
            if (mode_ == Mode::Server) clientfd_ = -1; else sockfd_ = -1;   // ★ 모드별로 닫기
        }
    }

    ~Sender() {
        if (sockfd_ >= 0) ::close(sockfd_);
        if (clientfd_ >= 0) ::close(clientfd_);       // ★ 추가
        if (listenfd_ >= 0) ::close(listenfd_);       // ★ 추가
    }

private:
    // ── connectLocked(): 원래 그대로, 하나도 안 바뀜 ──
    bool connectLocked() {
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

    enum class Mode { Client, Server };   // ★ 추가 (모드 종류)
    Mode mode_ = Mode::Client;            // ★ 추가 (현재 모드)
    int sockfd_ = -1;                     //   원래 있던 것
    int listenfd_ = -1;                   // ★ 추가 (listen 소켓)
    int clientfd_ = -1;                   // ★ 추가 (accept된 Qt 소켓)
    std::mutex mtx_;                      //   원래 있던 것
    std::string host_;                    //   원래 있던 것
    int port_ = 0;                        //   원래 있던 것
};

// 판단 매트릭스: 카메라 감지 + 센서값 융합 → 종합 상태 판정          // <- 처음
// 기획서 표의 "상태 판정" 부분. 자동 대응(사이렌·밸브)은 STM 연결 후 별도 모듈로
std::string judgeState(bool camFire, bool camSmoke, float gasPpm, float smokePpm) {
    bool gasHigh   = gasPpm   > 200.0f;   // 임계값: 유나님 센서 보정 후 확정
    bool smokeHigh = smokePpm > 150.0f;

    if (camFire  && gasHigh)   return "danger";   // 5행: 가스+화재 동시 (최고 위험)
    if (camFire)               return "danger";   // 4행: 화염 감지
    if (camSmoke && smokeHigh) return "danger";   // 3행: 연기 영상+센서 동시 반응
    if (gasHigh)               return "danger";   // 1행: 가스 누출 단독
    if (camSmoke)              return "warning";  // 2행: 연기 영상만 (관리자 대기)
    return "safe";
}

// mock 센서 스레드. 부품 오면 값 생성부만 실제 드라이버 읽기로 교체
void sensorWorker(Sender& sender) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    int tick = 0;

    while (true) {
        // 평상시 기준값 + 흔들림
        float temp     = 26.0f + jitter(rng) * 2.0f;
        float humidity = 45.0f + jitter(rng) * 5.0f;
        float gasPpm   = 45.0f + jitter(rng) * 10.0f;
        float smokePpm = 8.0f  + jitter(rng) * 3.0f;

        // 데모 스파이크: 60초 주기 중 45~60초 구간은 가스 급상승 (Qt 경고 UI 테스트용)
        if (tick % 60 >= 45) {
            gasPpm   = 250.0f + jitter(rng) * 30.0f;
            smokePpm = 180.0f + jitter(rng) * 20.0f;
        }

        // ── [카메라 상태 종합] 4채널 중 하나라도 감지면 true ──
        bool camFire = false, camSmoke = false;
        for (int i = 0; i < 4; i++) {
            if (detState[i].fire)  camFire  = true;
            if (detState[i].smoke) camSmoke = true;
        }

        // ── [JSON 조립] 명세서 "센서 정보" 스키마 그대로 ──
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "{\"type\":\"sensor\",\"zone\":\"A\""
            << ",\"ts\":" << std::time(nullptr)
            << ",\"temp\":" << temp
            << ",\"humidity\":" << humidity
            << ",\"gasPpm\":" << gasPpm
            << ",\"smokePpm\":" << smokePpm
            << ",\"state\":\"" << judgeState(camFire, camSmoke, gasPpm, smokePpm) << "\"}";
        sender.send(oss.str());

        tick++;
        std::this_thread::sleep_for(std::chrono::seconds(1));  // 전송 주기 1초
    }
}

// 채널 1개 담당. RTSP 연결 → 프레임 읽기 → 감지 → JSON 전송. 끊기면 재연결
void worker(int ch, FrameStore& store, Sender& sender) {
    std::string url = "rtsp://localhost:8554/cam" + std::to_string(ch + 1);

    FireDetectionRuntime runtime;   // 복사 금지 타입 → 채널당 지역변수 1개
    std::uint64_t frameId = 0;
    bool wasShowingBoxes = false;  

    while (true) {
        cv::VideoCapture cap;
        cap.open(url, cv::CAP_FFMPEG);
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);   // ★ 추가 — 버퍼 1프레임 = 항상 최신 처리

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

            if (frameId % 1 == 0) {
                runtime.submitFrame(frame, frameId);
            }
            frameId++;
            FireRuntimeSnapshot snap = runtime.poll();

            if (snap.boxIsFresh) {
                std::cout << "[cam" << ch + 1 << "] boxIsFresh! boxes="
                          << snap.detection.boxes.size()
                          << " alarm=" << snap.alarm.alarmActive << "\n";   // ★ 임시 디버그

                bool hasFire = false, hasSmoke = false;                      
                for (const auto& b : snap.detection.boxes) {
                    if (b.type == DetectionType::FIRE) hasFire = true;
                    else                               hasSmoke = true;
                }
                detState[ch].fire  = hasFire && snap.alarm.alarmActive;
                detState[ch].smoke = hasSmoke;                                
                          
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
                wasShowingBoxes = true;
            }
            else if (wasShowingBoxes && !snap.alarm.alarmActive) {                 
                std::ostringstream oss;
                oss << "{\"type\":\"detection\",\"channel\":" << (ch + 1)
                    << ",\"srcW\":" << frame.cols << ",\"srcH\":" << frame.rows
                    << ",\"alarm\":false,\"boxes\":[]}";
                sender.send(oss.str());
                wasShowingBoxes = false;               
                detState[ch].fire  = false;                               
                detState[ch].smoke = false; 
            }
        }

        cap.release();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

int main() {
    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS",
           "rtsp_transport;tcp|fflags;nobuffer|flags;low_delay", 1);   // ★ 저지연 옵션
    cv::setNumThreads(1);   // ★ OpenCV 채널당 1스레드 = 멀티채널 최적화 핵심
    Sender sender;
    sender.listen(9999);                    // [지금 테스트] Qt 직접 접속
    //sender.connect("127.0.0.1", 9999);   // mock 테스트 포트. 나중에 광렬 TLS 서버 포트로 교체

    FrameStore store;
    std::thread threads[4];

    std::thread sensorThread(sensorWorker, std::ref(sender));    
    sensorThread.detach();

    for (int i = 0; i < 4; i++) {
        threads[i] = std::thread(worker, i, std::ref(store), std::ref(sender));
    }

    for (int i = 0; i < 4; i++) {
        threads[i].join();
    }

    return 0;
}