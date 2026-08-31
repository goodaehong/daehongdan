// roi_store.h 구현. 감지 제외 영역의 검증 · 저장 · 복원과 버전 관리를 담당한다.

#include "roi_store.h"
#include "../json_util.h"

#include <atomic>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

namespace {

constexpr int kChannels = 4;

struct ChannelRoi {
    IgnoreRegionConfig fire;
    IgnoreRegionConfig smoke;
    std::string raw = "[]";     // regions 배열 원문 — label 등 서버가 안 쓰는 필드까지 그대로 왕복
    double threshold = 0.5;
};

std::mutex g_mtx;
ChannelRoi g_ch[kChannels];
std::atomic<int> g_ver[kChannels];

// applyTo 배열 원문에 해당 이름이 있는지. 필드가 없으면 양쪽 다 적용
bool hasTarget(const std::string& applyTo, const char* name) {
    if (applyTo.empty()) return true;
    return applyTo.find(name) != std::string::npos;
}

// regions 원문 → 화재용·연기용 config 2개. 실패하면 false + reason
bool buildConfigs(const std::string& regionsRaw, double threshold,
                  IgnoreRegionConfig& fire, IgnoreRegionConfig& smoke,
                  std::string& reason) {
    fire = IgnoreRegionConfig{};
    smoke = IgnoreRegionConfig{};
    fire.overlapThreshold = threshold;
    smoke.overlapThreshold = threshold;

    int idx = 0;
    for (const std::string& r : jsonArray(regionsRaw)) {
        idx++;
        IgnoreRegion region;
        region.enabled = jsonBool(r, "enabled", true);

        for (const std::string& p : jsonArray(jsonRaw(r, "points"))) {
            std::vector<std::string> xy = jsonArray(p);
            if (xy.size() != 2) {
                reason = std::to_string(idx) + "번 영역: 좌표 형식 오류";
                return false;
            }
            float x = (float)std::atof(xy[0].c_str());
            float y = (float)std::atof(xy[1].c_str());
            if (x < 0.f || x > 1.f || y < 0.f || y > 1.f) {
                reason = std::to_string(idx) + "번 영역: 좌표가 0~1 범위 밖";
                return false;
            }
            region.points.push_back(cv::Point2f(x, y));
        }
        if (region.points.size() < 3) {
            reason = std::to_string(idx) + "번 영역: 점 3개 미만";
            return false;
        }

        std::string applyTo = jsonRaw(r, "applyTo");
        if (hasTarget(applyTo, "fire"))  fire.regions.push_back(region);
        if (hasTarget(applyTo, "smoke")) smoke.regions.push_back(region);
    }
    return true;
}

}  // namespace

// 메시지 한 줄(또는 저장 파일의 채널 하나)을 파싱해서 저장
bool RoiStore_Apply(const std::string& line, int* chOut, std::string* reason) {
    int ch1 = jsonInt(line, "channel", 0);          // Qt는 1-based
    if (chOut) *chOut = ch1;
    if (ch1 < 1 || ch1 > kChannels) {
        if (reason) *reason = "channel 범위 밖 (1~4)";
        return false;
    }

    double thr = jsonDouble(line, "overlapThreshold", 0.5);
    std::string regionsRaw = jsonRaw(line, "regions");
    if (regionsRaw.empty()) regionsRaw = "[]";      // 사용자가 전부 지운 경우

    IgnoreRegionConfig fire, smoke;
    std::string why;
    if (!buildConfigs(regionsRaw, thr, fire, smoke, why)) {
        if (reason) *reason = why;
        return false;                               // 실패 시 기존 설정 유지
    }

    int ch = ch1 - 1;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_ch[ch].fire      = fire;
        g_ch[ch].smoke     = smoke;
        g_ch[ch].raw       = regionsRaw;
        g_ch[ch].threshold = thr;
    }
    g_ver[ch]++;                                    // worker가 이걸 보고 가져간다
    return true;
}

int RoiStore_Version(int ch) {
    if (ch < 0 || ch >= kChannels) return 0;
    return g_ver[ch].load();
}

void RoiStore_Get(int ch, IgnoreRegionConfig& fire, IgnoreRegionConfig& smoke) {
    if (ch < 0 || ch >= kChannels) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    fire  = g_ch[ch].fire;
    smoke = g_ch[ch].smoke;
}

double RoiStore_Threshold(int ch) {                      
    if (ch < 0 || ch >= kChannels) return 0.5;
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_ch[ch].threshold;
} 

std::string RoiStore_ToJson(int ch) {
    if (ch < 0 || ch >= kChannels) return "[]";
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_ch[ch].raw;
}

// 파일 형식: {"channels":[{"channel":1,"overlapThreshold":0.5,"regions":[...]}, ...]}
// 채널 하나의 모양이 Qt 메시지와 같아서 복원 때 RoiStore_Apply를 그대로 쓴다
bool RoiStore_Save() {
    std::ostringstream oss;
    oss << "{\"channels\":[";
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        for (int i = 0; i < kChannels; i++) {
            if (i) oss << ",";
            oss << "{\"channel\":" << (i + 1)
                << ",\"overlapThreshold\":" << g_ch[i].threshold
                << ",\"regions\":" << g_ch[i].raw << "}";
        }
    }
    oss << "]}";

    std::ofstream f(ROI_CONFIG_PATH, std::ios::trunc);
    if (!f) return false;
    f << oss.str();
    return f.good();
}

void RoiStore_Load() {
    std::ifstream f(ROI_CONFIG_PATH);
    if (!f) return;                                 // 파일 없으면 빈 상태로 시작
    std::stringstream ss;
    ss << f.rdbuf();

    for (const std::string& c : jsonArray(jsonRaw(ss.str(), "channels"))) {
        int ch = 0;
        std::string why;
        if (!RoiStore_Apply(c, &ch, &why))
            std::cerr << "[ROI] 저장 파일 복원 실패 (ch" << ch << "): " << why << "\n";
    }
}