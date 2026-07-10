#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>      // 스레드 사용
#include <atomic>      // 스레드 안전한 종료 플래그
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────
// 모든 스레드가 공유하는 "종료 신호"
//   - atomic: 여러 스레드가 동시에 건드려도 안전한 변수
//   - true가 되면 모든 스레드가 반복을 멈추고 정리
// ─────────────────────────────────────────────────────────
std::atomic<bool> g_stop(false);

// ─────────────────────────────────────────────────────────
// 한 채널을 담당하는 일꾼 함수
//   - 스레드 하나가 이 함수를 통째로 맡아서 돎
//   - url: 그 채널의 RTSP 주소
//   - winName: 그 채널을 띄울 창 이름
// ─────────────────────────────────────────────────────────
void channelWorker(const std::string& url, const std::string& winName) {
    // 이 채널 전용 카메라 연결
    cv::VideoCapture cap(url);

    if (!cap.isOpened()) {
        std::cerr << "[" << winName << "] 연결 실패: " << url << "\n";
        return;   // 이 스레드만 종료. 다른 채널은 계속 돎.
    }
    std::cout << "[" << winName << "] 연결 성공\n";

    cv::Mat frame;
    // 종료 신호가 오기 전까지 계속 프레임 받아서 표시
    while (!g_stop.load()) {
        bool ok = cap.read(frame);        // 프레임 수신 + 디코딩
        if (!ok || frame.empty()) {
            std::cerr << "[" << winName << "] 프레임 끊김\n";
            break;   // 이 채널만 빠짐. 나머지는 유지.
        }
        cv::imshow(winName, frame);        // 이 채널 창에 표시

        // 주의: waitKey는 원래 메인 스레드에서 처리하는 게 안전하지만,
        // 지금은 각 창을 각 스레드가 그리는 단순 구조로 감 (학습용).
        // 1ms 대기 (화면 갱신에 필요). 종료키 처리는 메인이 담당.
        cv::waitKey(1);
    }

    cap.release();
    std::cout << "[" << winName << "] 종료\n";
}

int main() {
    // ─────────────────────────────────────────────────────
    // 4채널 주소 준비 (채널 번호만 0,1,2,3 으로 다름)
    //   비번/IP는 네 카메라에 맞게. 특수문자 !는 그대로 둠.
    // ─────────────────────────────────────────────────────
    std::string base_front = "rtsp://admin:5hanwha!@172.20.35.144:554/";
    std::string base_back  = "/profile2/media.smp";

    std::vector<std::string> urls;
    std::vector<std::string> names;
    for (int ch = 0; ch < 4; ch++) {
        urls.push_back(base_front + std::to_string(ch) + base_back);
        names.push_back("RTSP Channel " + std::to_string(ch));
    }

    // ─────────────────────────────────────────────────────
    // 스레드 4개 생성 — 각자 한 채널씩 맡김
    // ─────────────────────────────────────────────────────
    std::vector<std::thread> threads;
    for (int ch = 0; ch < 4; ch++) {
        threads.emplace_back(channelWorker, urls[ch], names[ch]);
    }

    std::cout << "4채널 수신 시작. 이 터미널에서 Enter 누르면 종료.\n";

    // ─────────────────────────────────────────────────────
    // 메인 스레드는 여기서 대기.
    //   사용자가 Enter 누르면 종료 신호를 켠다.
    // ─────────────────────────────────────────────────────
    std::cin.get();          // Enter 기다림
    g_stop.store(true);      // 모든 스레드에게 "그만!" 신호

    // ─────────────────────────────────────────────────────
    // 모든 스레드가 깔끔히 끝날 때까지 기다림 (join)
    // ─────────────────────────────────────────────────────
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    cv::destroyAllWindows();
    std::cout << "전체 종료.\n";
    return 0;
}
