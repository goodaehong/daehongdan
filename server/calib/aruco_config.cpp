#include "aruco_config.h"
#include "../json_util.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include <chrono>                                      
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>  

namespace {

constexpr int MIN_MARKERS = 4;   // 스크립트와 동일 기준 (ConfigureArucoChannel.sh:106)

struct Marker {
    int    id = -1;
    double x = 0, y = 0;
    double sizeM = 0;
    double rotation = 0;
};

std::mutex       g_mtx;
bool             g_running[4] = {false, false, false, false};   // 채널별 계산 중 여부
CalibRunDoneFn   g_onDone = nullptr;
std::vector<std::thread> g_threads;
bool             g_cancel[4] = {false, false, false, false};   // 중단 요청 플래그   

// 정상 계산은 max-frames 900 / 10fps = 약 90초가 상한. 두 배 여유를 둔다.
// RTSP 가 아예 안 붙으면 프레임 수가 안 늘어 스스로 끝나지 않기 때문에 필요하다
constexpr int CALIB_TIMEOUT_SEC = 180;

// 끝날 때 공통 처리 — 상태 풀고 결과 알림. 빠뜨리면 그 채널이 영영 잠긴다
void finish(int ch, CalibRunResult r, const std::string& detail) {
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_running[ch - 1] = false;
        g_cancel[ch - 1]  = false;
    }
    const char* word = (r == CalibRunResult::Ok)        ? "성공"
                     : (r == CalibRunResult::Cancelled) ? "중단됨"
                     : (r == CalibRunResult::Timeout)   ? "시간 초과" : "실패";
    std::cout << "[보정] cam" << ch << " 계산 " << word
              << (detail.empty() ? "" : " — " + detail) << "\n";
    if (g_onDone) g_onDone(ch, r, detail);
}                                                                         

std::string fmt(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.8g", v);   // 스크립트의 awk %.8g 와 동일
    return buf;
}

bool inRange(double v, double lo, double hi) { return v >= lo && v <= hi; }

// 다른 채널의 BOARD/MARKER 줄만 골라낸다. 한 채널을 고쳐도 나머지가 남아야 한다
std::vector<std::string> otherChannelLines(const std::string& path, int ch) {
    std::vector<std::string> out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream is(line);
        std::string key;
        int owner = 0;
        if (!(is >> key >> owner)) continue;
        if ((key == "BOARD" || key == "MARKER") && owner != ch)
            out.push_back(line);
    }
    return out;
}

// 좌표가 바뀌면 기존 변환표는 틀린 값이 된다. 지우지 않고 이름만 바꿔 치운다
void retireHomography(int ch, const std::string& stamp) {
    const std::string p = std::string(FIRE_STATIC_HOMOGRAPHY_DIR)
                        + "/homography_ch" + std::to_string(ch) + ".yml";
    std::ifstream f(p);
    if (!f) return;
    f.close();
    const std::string to = p + "." + stamp + ".stale.bak";
    if (std::rename(p.c_str(), to.c_str()) == 0)
        std::cout << "[보정] cam" << ch << " 기존 변환표 치움 — " << to << "\n";
}

std::string timeStamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

}  // namespace

bool ArucoConfig_Apply(const std::string& line, int* chOut, std::string* reason) {
    // 마커 번호를 사유에 끼워 넣는 곳이 있어 std::string 으로 받는다      
    auto fail = [&](const std::string& why) { if (reason) *reason = why; return false; };   

    const int ch = jsonInt(line, "channel", 0);
    if (chOut) *chOut = ch;
    if (ch < 1 || ch > 4) return fail("채널 번호가 1~4 범위를 벗어납니다");

    // ── 공장 전체 범위 ──
    const std::string fac = jsonRaw(line, "factory");
    const double fx0 = jsonDouble(fac, "minX", 0), fy0 = jsonDouble(fac, "minY", 0);
    const double fx1 = jsonDouble(fac, "maxX", 0), fy1 = jsonDouble(fac, "maxY", 0);
    if (fx1 <= fx0 || fy1 <= fy0) return fail("공장 범위가 잘못되었습니다 (최댓값이 최솟값보다 커야 함)");

    const double scale = jsonDouble(line, "modelScale", 0);
    if (scale <= 0) return fail("모형 축척은 0보다 커야 합니다");

    // ── 이 채널이 맡는 범위 ──
    const std::string brd = jsonRaw(line, "board");
    const double bx0 = jsonDouble(brd, "minX", 0), by0 = jsonDouble(brd, "minY", 0);
    const double bx1 = jsonDouble(brd, "maxX", 0), by1 = jsonDouble(brd, "maxY", 0);
    if (bx1 <= bx0 || by1 <= by0) return fail("채널 범위가 잘못되었습니다");
    if (!inRange(bx0, fx0, fx1) || !inRange(bx1, fx0, fx1) ||
        !inRange(by0, fy0, fy1) || !inRange(by1, fy0, fy1))
        return fail("채널 범위가 공장 범위 밖입니다");

    // ── 마커 목록 ──
    std::vector<Marker> markers;
    for (const std::string& e : jsonArray(jsonRaw(line, "markers"))) {
        Marker m;
        m.id       = jsonInt(e, "id", -1);
        m.x        = jsonDouble(e, "x", 0);
        m.y        = jsonDouble(e, "y", 0);
        m.sizeM    = jsonDouble(e, "sizeCm", 0) / 100.0;   // Qt는 cm, 파일은 m
        m.rotation = jsonDouble(e, "rotation", 0);

        if (m.id < 0 || m.id > 49) return fail("마커 ID는 0~49 범위여야 합니다 (DICT_4X4_50)");
        if (m.sizeM <= 0) return fail("마커 크기가 0 이하입니다");
        if (!inRange(m.x, bx0, bx1) || !inRange(m.y, by0, by1))
            return fail("마커 " + std::to_string(m.id) + "번이 채널 범위 밖에 있습니다");
        for (const auto& p : markers)
            if (p.id == m.id) return fail("마커 ID " + std::to_string(m.id) + "번이 중복됩니다");
        markers.push_back(m);
    }
    if ((int)markers.size() < MIN_MARKERS)
        return fail("마커가 " + std::to_string(MIN_MARKERS) + "개 이상 필요합니다");

    // ── 파일 만들기 ──
    const std::string path  = FIRE_ARUCO_CONFIG_PATH;
    const std::string stamp = timeStamp();
    const auto others = otherChannelLines(path, ch);

    std::ostringstream o;
    o << "# Fixed ArUco installation geometry. Coordinates are real factory metres.\n"
      << "VERSION 1\n"
      << "DICTIONARY DICT_4X4_50\n"
      << "GRID 60 0 59\n"
      << "FACTORY " << fmt(fx0) << " " << fmt(fy0) << " " << fmt(fx1) << " " << fmt(fy1) << "\n"
      << "MODEL_SCALE " << fmt(scale) << "\n"
      << "# minimum markers, minimum inlier corners, maximum RMS px, hold ms, update frames, smoothing\n"
      << "QUALITY 4 12 2.0 1500 1 0.45\n\n";
    for (const auto& l : others) o << l << "\n";
    o << "\n# Channel " << ch << "\n"
      << "BOARD " << ch << " " << fmt(bx0) << " " << fmt(by0) << " "
                             << fmt(bx1) << " " << fmt(by1) << "\n";
    for (const auto& m : markers)
        o << "MARKER " << ch << " " << m.id << " " << fmt(m.x) << " " << fmt(m.y)
          << " " << fmt(m.sizeM) << " " << fmt(m.rotation) << "\n";

    // 덮어쓰기 전에 백업. 잘못 넣어도 되돌릴 수 있어야 한다
    {
        std::ifstream cur(path, std::ios::binary);
        if (cur) {
            std::ofstream bak(path + "." + stamp + ".bak", std::ios::binary);
            bak << cur.rdbuf();
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) return fail("설정 파일을 쓸 수 없습니다");
    out << o.str();
    if (!out.good()) return fail("설정 파일 쓰기에 실패했습니다");
    out.close();

    retireHomography(ch, stamp);
    std::cout << "[보정] cam" << ch << " 좌표 설정 저장 (마커 " << markers.size() << "개)\n";
    return true;
}

// 저장된 좌표를 Qt 폼에 되돌려준다. 재보정할 때 숫자를 다시 치지 않게.     
// 파일이 사람이 읽는 텍스트 형식이라 여기서 JSON 으로 옮긴다
std::string ArucoConfig_ToJson(int ch) {
    if (ch < 1 || ch > 4) return "";
    std::ifstream f(FIRE_ARUCO_CONFIG_PATH);
    if (!f) return "";

    double fx0 = 0, fy0 = 0, fx1 = 0, fy1 = 0, scale = 0;
    double bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    bool haveBoard = false;
    std::ostringstream markers;
    int markerCount = 0;

    std::string line;
    while (std::getline(f, line)) {
        std::istringstream is(line);
        std::string key;
        if (!(is >> key)) continue;

        if (key == "FACTORY") { is >> fx0 >> fy0 >> fx1 >> fy1; }
        else if (key == "MODEL_SCALE") { is >> scale; }
        else if (key == "BOARD") {
            int owner = 0;
            is >> owner;
            if (owner != ch) continue;
            is >> bx0 >> by0 >> bx1 >> by1;
            haveBoard = true;
        }
        else if (key == "MARKER") {
            int owner = 0, id = 0;
            double x = 0, y = 0, sizeM = 0, rot = 0;
            is >> owner;
            if (owner != ch) continue;
            is >> id >> x >> y >> sizeM >> rot;
            if (markerCount++) markers << ",";
            markers << "{\"id\":" << id
                    << ",\"x\":" << fmt(x) << ",\"y\":" << fmt(y)
                    << ",\"sizeCm\":" << fmt(sizeM * 100.0)   // 파일은 m, Qt는 cm
                    << ",\"rotation\":" << fmt(rot) << "}";
        }
    }
    if (!haveBoard) return "";   // 이 채널은 아직 설정 전

    std::ostringstream o;
    o << "\"channel\":" << ch
      << ",\"factory\":{\"minX\":" << fmt(fx0) << ",\"minY\":" << fmt(fy0)
      <<             ",\"maxX\":" << fmt(fx1) << ",\"maxY\":" << fmt(fy1) << "}"
      << ",\"modelScale\":" << fmt(scale)
      << ",\"board\":{\"minX\":" << fmt(bx0) << ",\"minY\":" << fmt(by0)
      <<           ",\"maxX\":" << fmt(bx1) << ",\"maxY\":" << fmt(by1) << "}"
      << ",\"markers\":[" << markers.str() << "]";
    return o.str();
}                                                                           

void ArucoConfig_SetOnDone(CalibRunDoneFn fn) { g_onDone = fn; }

bool ArucoConfig_IsRunning(int ch) {
    if (ch < 1 || ch > 4) return false;
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_running[ch - 1];
}

bool ArucoConfig_CancelCalibration(int ch, std::string* reason) {
    if (ch < 1 || ch > 4) { if (reason) *reason = "채널 번호가 1~4 범위를 벗어납니다"; return false; }
    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_running[ch - 1]) { if (reason) *reason = "계산 중이 아닙니다"; return false; }
    g_cancel[ch - 1] = true;   // 감시 루프가 다음 바퀴에 집어간다
    std::cout << "[보정] cam" << ch << " 중단 요청\n";
    return true;
}

bool ArucoConfig_RunCalibration(int ch, std::string* reason) {
    if (ch < 1 || ch > 4) { if (reason) *reason = "채널 번호가 1~4 범위를 벗어납니다"; return false; }
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_running[ch - 1]) { if (reason) *reason = "이미 계산 중입니다"; return false; }
        g_running[ch - 1] = true;
        g_cancel[ch - 1]  = false;
    }

    // popen 은 프로세스 번호를 안 알려줘서 중단·시간제한을 걸 수 없다.
    // 직접 띄우고 번호를 들고 있어야 셋 다(완료·시간초과·취소) 처리된다
    g_threads.emplace_back([ch] {
        const std::string script = std::string(CALIB_SCRIPT_DIR)
                                 + "/RunFixedHomographyCalibration.sh";
        const std::string chArg = std::to_string(ch);

        pid_t pid = ::fork();
        if (pid < 0) {
            finish(ch, CalibRunResult::Error, "계산 프로세스를 만들 수 없습니다");
            return;
        }
        if (pid == 0) {
            // 자식: 새 프로세스 그룹으로 묶는다. 스크립트가 계산기를 또 자식으로
            // 띄우기 때문에, 그룹째 정리해야 고아 프로세스가 안 남는다
            ::setsid();
            ::execl("/bin/bash", "bash", script.c_str(), chArg.c_str(), (char*)nullptr);
            ::_exit(127);   // execl 이 돌아왔다 = 실행 실패
        }

        // 부모: 0.5초마다 확인 — 끝났나 / 시간 넘었나 / 취소됐나
        const long deadline = (long)std::time(nullptr) + CALIB_TIMEOUT_SEC;
        for (;;) {
            int status = 0;
            const pid_t r = ::waitpid(pid, &status, WNOHANG);
            if (r == pid) {
                const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                finish(ch, ok ? CalibRunResult::Ok : CalibRunResult::Error,
                       ok ? "" : "계산에 실패했습니다 (마커가 충분히 보이는지 확인 필요)");
                return;
            }
            if (r < 0) { finish(ch, CalibRunResult::Error, "계산 상태를 확인할 수 없습니다"); return; }

            bool cancelled = false;
            { std::lock_guard<std::mutex> lk(g_mtx); cancelled = g_cancel[ch - 1]; }
            const bool timedOut = (long)std::time(nullptr) >= deadline;

            if (cancelled || timedOut) {
                ::kill(-pid, SIGKILL);          // 그룹째 (계산기까지 같이)
                ::waitpid(pid, nullptr, 0);
                finish(ch, cancelled ? CalibRunResult::Cancelled : CalibRunResult::Timeout,
                       cancelled ? "" : "계산 시간이 " + std::to_string(CALIB_TIMEOUT_SEC)
                                      + "초를 넘겼습니다");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
    std::cout << "[보정] cam" << ch << " 계산 시작\n";
    return true;
}

void ArucoConfig_Shutdown() {
    for (int c = 1; c <= 4; c++) {
        std::string ignore;
        ArucoConfig_CancelCalibration(c, &ignore);   // 돌고 있으면 중단시켜 스레드를 풀어준다
    }
    for (auto& t : g_threads) if (t.joinable()) t.join();
    g_threads.clear();
}
