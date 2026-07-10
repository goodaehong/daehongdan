#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>       // 저장소 보호용 자물쇠
#include <vector>
#include <string>

const int NUM_CH = 4;

// ─────────────────────────────────────────────────────────
// 공용 저장소: 각 채널의 "최신 프레임" 4칸
//   - g_frames[i] : i번 채널의 최신 그림
//   - g_mutex     : 저장소를 보호하는 자물쇠
//                   (스레드가 넣는 순간 메인이 꺼내면 꼬이므로,
//                    한 번에 한 명만 건드리게 잠금)
// ─────────────────────────────────────────────────────────
std::vector<cv::Mat> g_frames(NUM_CH);
std::mutex g_mutex;
std::atomic<bool> g_stop(false);

// ─────────────────────────────────────────────────────────
// 스레드 일: 자기 채널 프레임을 받아서 저장소에 "넣기만" 한다.
//   (imshow 절대 안 함 → 화면 그리기는 메인 담당)
// ─────────────────────────────────────────────────────────
void captureWorker(int idx, const std::string& url) {
    cv::VideoCapture cap(url);
    if (!cap.isOpened()) {
        std::cerr << "[ch" << idx << "] 연결 실패\n";
        return;
    }
    std::cout << "[ch" << idx << "] 연결 성공\n";

    cv::Mat frame;
    while (!g_stop.load()) {
        if (!cap.read(frame) || frame.empty()) {
            std::cerr << "[ch" << idx << "] 프레임 끊김\n";
            break;
        }
        // 저장소에 넣기 (자물쇠 잠그고 → 복사 → 풀기)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_frames[idx] = frame.clone();   // clone: 안전하게 복사본 저장
        }
    }
    cap.release();
    std::cout << "[ch" << idx << "] 수신 종료\n";
}

int main() {
    // 4채널 주소 만들기 (채널 번호만 0~3)
    std::string front = "rtsp://admin:5hanwha!@172.20.35.144:554/";
    std::string back  = "/profile2/media.smp";

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_CH; i++) {
        std::string url = front + std::to_string(i) + back;
        threads.emplace_back(captureWorker, i, url);
    }

    std::cout << "수신 시작. 영상 창에서 ESC 누르면 종료.\n";

    // 한 칸 크기 (각 채널을 이 크기로 줄여서 2x2로 배치)
    const int CW = 640, CH = 360;

    // ─────────────────────────────────────────────────────
    // 메인 스레드: 저장소에서 4장 꺼내 → 2x2로 합쳐 → 한 창에 표시
    // ─────────────────────────────────────────────────────
    while (!g_stop.load()) {
        // 2x2 그리드용 검은 배경 (가로 2칸, 세로 2칸)
        cv::Mat grid(CH * 2, CW * 2, CV_8UC3, cv::Scalar(0, 0, 0));

        // 저장소에서 현재 프레임들 꺼내오기 (자물쇠)
        std::vector<cv::Mat> local(NUM_CH);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (int i = 0; i < NUM_CH; i++) {
                if (!g_frames[i].empty())
                    local[i] = g_frames[i].clone();
            }
        }

        // 각 채널을 칸 크기로 줄여서 그리드의 제 위치에 붙이기
        for (int i = 0; i < NUM_CH; i++) {
            if (local[i].empty()) continue;   // 아직 프레임 없으면 검은칸 유지

            cv::Mat resized;
            cv::resize(local[i], resized, cv::Size(CW, CH));

            int row = i / 2;   // 0,1 → 0행 / 2,3 → 1행
            int col = i % 2;   // 0,2 → 0열 / 1,3 → 1열
            // 그리드에서 이 칸의 영역을 지정해서 거기에 복사
            resized.copyTo(grid(cv::Rect(col * CW, row * CH, CW, CH)));
        }

        cv::imshow("4CH Grid", grid);        // 합친 이미지 한 장을 한 창에
        if (cv::waitKey(1) == 27) {          // ESC → 종료
            g_stop.store(true);
        }
    }

    // 스레드 정리
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    cv::destroyAllWindows();
    std::cout << "전체 종료.\n";
    return 0;
}
