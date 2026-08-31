// clip_recorder.h 구현. 링 버퍼 관리와 별도 스레드에서의 mp4 인코딩을 담당한다.

#include "clip_recorder.h"

#include <opencv2/opencv.hpp>
#include <sys/stat.h>

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr int CLIP_FPS       = 10;   // 감지 프로파일이 360p/10fps — 들어오는 속도에 맞춘다
constexpr int PRE_SEC        = 3;
constexpr int POST_SEC       = 10;
constexpr int RING_MAX       = PRE_SEC  * CLIP_FPS;
constexpr int POST_MAX       = POST_SEC * CLIP_FPS;
constexpr int JPEG_QUALITY   = 70;
constexpr int CLIP_WIDTH     = 640;   // 워커는 720p를 받는다. 그대로 인코딩하면       
constexpr int CLIP_MIN_SRC_W = 720;   // 코어 60%(측정치) — 오탐 판별엔 360p면 충분   
constexpr int POST_GRACE_SEC = 5;    // 카메라가 끊겨도 이만큼 지나면 모인 것만 저장

struct Job {
    int         ch = 0;
    long        incidentId = 0;
    long        ts = 0;                    // 클립 시작 시각 (DB 행 매칭용)
    std::string path;
    std::vector<std::vector<uchar>> frames;   // 앞 3초 + 뒤 10초
    int         postWant = POST_MAX;          // 더 받아야 할 프레임 수
    long        deadline = 0;
};

std::mutex                       g_mtx;
std::condition_variable          g_cv;
std::deque<std::vector<uchar>>   g_ring[4];
std::shared_ptr<Job>             g_active[4];   // 채널당 동시 1건
std::deque<std::shared_ptr<Job>> g_queue;       // 저장 대기
bool                             g_running = false;
std::thread                      g_saver;
ClipSavedFn                      g_onSaved;

// jpg 목록 → mp4. 라즈베리에서 확실히 열리는 mp4v 사용 (avc1은 없는 빌드가 많음)
bool writeMp4(const std::shared_ptr<Job>& job) {
    cv::Size size;
    std::vector<cv::Mat> mats;
    mats.reserve(job->frames.size());
    for (const auto& enc : job->frames) {
        cv::Mat m = cv::imdecode(enc, cv::IMREAD_COLOR);
        if (m.empty()) continue;
        if (size.width == 0) size = m.size();
        else if (m.size() != size) cv::resize(m, m, size);   // 도중 해상도가 바뀌면 첫 장 기준
        mats.push_back(m);
    }
    if (mats.empty()) return false;

    // H.264(avc1). Qt의 Windows 디코더가 확실히 재생하고 용량도 절반이다.
    // 빌드에 H.264가 빠진 환경도 있어 안 열리면 MPEG-4로 물러선다
    cv::VideoWriter vw(job->path, cv::VideoWriter::fourcc('a', 'v', 'c', '1'),
                       CLIP_FPS, size);
    if (!vw.isOpened())
        vw.open(job->path, cv::VideoWriter::fourcc('m', 'p', '4', 'v'),
                CLIP_FPS, size);
    if (!vw.isOpened()) return false;
    for (const auto& m : mats) vw.write(m);
    vw.release();
    return true;
}

void saverLoop() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lk(g_mtx);
            g_cv.wait_for(lk, std::chrono::milliseconds(500),
                          [] { return !g_queue.empty() || !g_running; });

            // 프레임이 끊기면 postWant가 안 줄어 영영 안 끝난다 → 시한으로 걷어냄
            long now = (long)std::time(nullptr);
            for (int ch = 0; ch < 4; ch++) {
                if (g_active[ch] && now >= g_active[ch]->deadline) {
                    g_queue.push_back(g_active[ch]);
                    g_active[ch].reset();
                }
            }
            if (g_queue.empty()) {
                if (!g_running) return;
                continue;
            }
            job = g_queue.front();
            g_queue.pop_front();
        }

        // 인코딩은 잠금 밖에서 — 수 초 걸려서 감지 스레드를 막으면 안 된다
        if (!writeMp4(job)) {
            std::cerr << "[클립] 저장 실패 — " << job->path << "\n";
            continue;
        }
        std::cout << "[클립] 저장 " << job->path
                  << " (" << job->frames.size() << "장)\n";
        if (g_onSaved) g_onSaved(job->incidentId, job->ts, job->path);
    }
}

}  // namespace

void ClipRecorder_Init(ClipSavedFn onSaved) {
    ::mkdir(CLIP_DIR, 0755);
    g_onSaved = std::move(onSaved);   // 스레드 시작 전에만 설정 → 잠금 불필요
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_running) return;
        g_running = true;
    }
    g_saver = std::thread(saverLoop);
}

void ClipRecorder_Push(int ch, const cv::Mat& frame) {
    if (ch < 0 || ch >= 4 || frame.empty()) return;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (!g_running) return;
    }

    // 720p를 그대로 인코딩하면 4채널에 코어 60%를 먹는다(라즈베리 측정치).
    // 클립은 오탐 판별용이라 360p로 줄여도 충분 → 코어 20%로 내려간다
    cv::Mat small;
    if (frame.cols >= CLIP_MIN_SRC_W)
        cv::resize(frame, small,
                   cv::Size(CLIP_WIDTH, frame.rows * CLIP_WIDTH / frame.cols));
    else
        small = frame;   // 이미 저해상도 스트림이면 그대로

    // 인코딩은 잠금 밖에서 (감지 스레드를 오래 붙잡으면 프레임이 밀림)
    std::vector<uchar> enc;
    std::vector<int> params = {cv::IMWRITE_JPEG_QUALITY, JPEG_QUALITY};
    if (!cv::imencode(".jpg", small, enc, params)) return;

    std::lock_guard<std::mutex> lk(g_mtx);
    g_ring[ch].push_back(enc);
    if ((int)g_ring[ch].size() > RING_MAX) g_ring[ch].pop_front();

    if (g_active[ch]) {
        g_active[ch]->frames.push_back(std::move(enc));
        if (--g_active[ch]->postWant <= 0) {
            g_queue.push_back(g_active[ch]);
            g_active[ch].reset();
            g_cv.notify_one();
        }
    }
}

bool ClipRecorder_Start(int ch, const std::string& zone, long ts, long incidentId) {
    if (ch < 0 || ch >= 4) return false;
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_running || g_active[ch]) return false;

    auto job = std::make_shared<Job>();
    job->ch         = ch;
    job->incidentId = incidentId;
    job->ts         = ts; 
    job->path       = std::string(CLIP_DIR) + "/" + zone + "_" + std::to_string(ts)
                    + "_cam" + std::to_string(ch + 1) + ".mp4";
    job->frames.assign(g_ring[ch].begin(), g_ring[ch].end());   // 불나기 직전 3초
    job->deadline   = (long)std::time(nullptr) + POST_SEC + POST_GRACE_SEC;
    g_active[ch] = job;
    return true;
}

std::string ClipRecorder_ReadBase64(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::string raw((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());

    static const char* TBL = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "abcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((raw.size() + 2) / 3 * 4);
    for (size_t i = 0; i < raw.size(); i += 3) {
        unsigned v = (unsigned char)raw[i] << 16;
        if (i + 1 < raw.size()) v |= (unsigned char)raw[i + 1] << 8;
        if (i + 2 < raw.size()) v |= (unsigned char)raw[i + 2];
        out += TBL[(v >> 18) & 63];
        out += TBL[(v >> 12) & 63];
        out += (i + 1 < raw.size()) ? TBL[(v >> 6) & 63] : '=';
        out += (i + 2 < raw.size()) ? TBL[v & 63]        : '=';
    }
    return out;
}

void ClipRecorder_Shutdown() {
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (!g_running) return;
        g_running = false;
        for (int ch = 0; ch < 4; ch++)          // 녹화 중이던 것도 있는 데까지 저장
            if (g_active[ch]) { g_queue.push_back(g_active[ch]); g_active[ch].reset(); }
    }
    g_cv.notify_all();
    if (g_saver.joinable()) g_saver.join();
}