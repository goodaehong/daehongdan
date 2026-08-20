#include "calib_store.h"

#include <mutex>
#include <sstream>

namespace {

struct ChannelState {
    CalibStage  stage = CalibStage::Unknown;
    std::string detail;                  // 실패 사유 (파일 못 읽은 이유 등)
    ArucoMappingStatus live;             // 워커가 매 프레임 갱신
    bool        hasLive = false;
    int         reloadVersion = 0;       // Qt 재로드 요청 횟수
};

std::mutex   g_mtx;
ChannelState g_ch[4];

const char* stageName(CalibStage s) {
    switch (s) {
        case CalibStage::NoBoard:      return "미설정";
        case CalibStage::NoLens:       return "렌즈 보정값 없음";
        case CalibStage::NoHomography: return "보정 미완료";
        case CalibStage::Ready:        return "적용됨";
        default:                       return "미확인";
    }
}

// Qt가 "다음에 뭘 해야 하는지" 바로 알 수 있게 단계마다 안내를 붙인다
const char* stageHint(CalibStage s) {
    switch (s) {
        case CalibStage::NoBoard:      return "마커 배치도 파일 필요";
        case CalibStage::NoLens:       return "렌즈 보정 필요";
        case CalibStage::NoHomography: return "마커 촬영 후 보정 계산 필요";
        case CalibStage::Ready:        return "";
        default:                       return "서버 시작 대기";
    }
}

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if      (c == '"')  o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += " ";
        else o += c;
    }
    return o;
}

}  // namespace

void CalibStore_SetStage(int ch, CalibStage stage, const std::string& detail) {
    if (ch < 0 || ch >= 4) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    g_ch[ch].stage  = stage;
    g_ch[ch].detail = detail;
    if (stage != CalibStage::Ready) g_ch[ch].hasLive = false;   // 실패면 옛 실시간 값은 무의미
}

void CalibStore_SetLive(int ch, const ArucoMappingStatus& st) {
    if (ch < 0 || ch >= 4) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    g_ch[ch].live    = st;
    g_ch[ch].hasLive = true;
}

void CalibStore_RequestReload(int ch) {
    if (ch < 0 || ch >= 4) return;
    std::lock_guard<std::mutex> lk(g_mtx);
    g_ch[ch].reloadVersion++;
}

int CalibStore_ReloadVersion(int ch) {
    if (ch < 0 || ch >= 4) return 0;
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_ch[ch].reloadVersion;
}

std::string CalibStore_ToJson() {
    std::lock_guard<std::mutex> lk(g_mtx);
    std::ostringstream o;
    o << "\"channels\":[";
    for (int i = 0; i < 4; i++) {
        const ChannelState& c = g_ch[i];
        if (i) o << ",";
        o << "{\"channel\":" << (i + 1)
          << ",\"stage\":\""  << stageName(c.stage) << "\""
          << ",\"ready\":"    << (c.stage == CalibStage::Ready ? "true" : "false")
          << ",\"hint\":\""   << stageHint(c.stage) << "\""
          << ",\"detail\":\"" << esc(c.detail) << "\"";

        // 실시간 값은 좌표 계산기가 실제로 도는 채널에서만 의미가 있다.       
        // 배치도가 없으면 마커를 세지도 않아 0/0이 나오는데,
        // 그걸 보내면 "보정됐는데 마커가 안 보임"과 구분이 안 된다
        if (c.hasLive && c.live.configured) {                 
            o << ",\"detectedMarkers\":" << c.live.detectedMarkers
              << ",\"acceptedMarkers\":" << c.live.acceptedMarkers
              << ",\"errorPx\":"         << c.live.reprojectionRmsPx
              << ",\"lensApplied\":"     << (c.live.lensCalibrationApplied ? "true" : "false")
              << ",\"staticHomography\":"<< (c.live.staticHomography ? "true" : "false");
        }
        o << "}";
    }
    o << "]";
    return o.str();
}