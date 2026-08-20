#include "floormap_store.h"
#include "../json_util.h"
#include "EvacPlanner.h" 

#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

namespace {

std::mutex g_mtx;
std::string g_resultJson;   // 변환 결과 원문. 아직 없으면 빈 문자열

// base64 글자 → 6비트 값. 표에 없는 글자는 -1
const std::array<int8_t, 256> kB64 = [] {
    std::array<int8_t, 256> t{};
    t.fill(-1);
    const char* set = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) t[(unsigned char)set[i]] = (int8_t)i;
    return t;
}();

// 글자 4개가 바이트 3개로 돌아간다. 6비트씩 모아 8비트가 차면 뱉는 방식
bool base64Decode(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size() * 3 / 4);
    int val = 0, bits = 0;
    for (unsigned char c : in) {
        if (c == '=') break;                        // 패딩 이후엔 데이터 없음
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int8_t d = kB64[c];
        if (d < 0) return false;                    // 깨진 문자 → 실패
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((char)((val >> bits) & 0xFF)); }
    }
    return !out.empty();
}

// PNG는 항상 이 8바이트로 시작한다. 전송 중 잘린 업로드를 여기서 걸러낸다
bool looksLikePng(const std::string& d) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (d.size() < 8) return false;
    for (int i = 0; i < 8; i++)
        if ((unsigned char)d[i] != sig[i]) return false;
    return true;
}

// EvacPlanner 결과 → Qt 응답 본문. 실패하면 빈 문자열 + reason          
// EvacPlanner가 실패를 따로 알려주지 않아 결과 자체를 검사한다
std::string buildResultJson(const std::string& imagePath, std::string& reason) {
    std::vector<std::vector<int>> bitmap;
    std::vector<Point> displays, exits;
    std::vector<std::vector<Point>> routes;
    try {
        bitmap   = getEvacBitmap(imagePath);
        displays = getEvacDisplays(imagePath);
        exits    = getEvacExits(imagePath);
        routes   = processFloorPlan(imagePath);
    } catch (const std::exception& e) {
        reason = std::string("변환 중 오류: ") + e.what();
        return "";
    }

    if ((int)bitmap.size() != GRID_SIZE) { reason = "격자 크기 불일치"; return ""; }
    for (const auto& row : bitmap)
        if ((int)row.size() != GRID_SIZE) { reason = "격자 행 길이 불일치"; return ""; }
    if (displays.empty()) { reason = "전광판을 찾지 못함"; return ""; }
    if (exits.empty())    { reason = "출구를 찾지 못함"; return ""; }
    if (routes.size() != displays.size() * exits.size()) {
        reason = "경로 개수가 전광판수 × 출구수와 다름";
        return "";
    }

    std::ostringstream o;
    o << "\"gridSize\":" << GRID_SIZE << ",\"bitmap\":[";
    for (size_t y = 0; y < bitmap.size(); y++) {
        if (y) o << ",";
        o << "[";
        for (size_t x = 0; x < bitmap[y].size(); x++) {
            if (x) o << ",";
            o << bitmap[y][x];
        }
        o << "]";
    }
    o << "],\"displays\":[";
    for (size_t i = 0; i < displays.size(); i++) {
        if (i) o << ",";
        o << "{\"id\":" << (i + 1) << ",\"y\":" << displays[i].y
          << ",\"x\":" << displays[i].x << "}";
    }
    o << "],\"exits\":[";
    for (size_t i = 0; i < exits.size(); i++) {
        if (i) o << ",";
        o << "{\"id\":" << (i + 1) << ",\"y\":" << exits[i].y
          << ",\"x\":" << exits[i].x << "}";
    }
    // routes 순서는 (전광판1→출구1,2,…), (전광판2→…) — EvacPlanner 규칙 그대로
    o << "],\"routes\":[";
    for (size_t i = 0; i < routes.size(); i++) {
        if (i) o << ",";
        o << "{\"displayId\":" << (i / exits.size() + 1)
          << ",\"exitId\":" << (i % exits.size() + 1)
          << ",\"waypoints\":[";
        for (size_t j = 0; j < routes[i].size(); j++) {
            if (j) o << ",";
            o << "{\"y\":" << routes[i][j].y << ",\"x\":" << routes[i][j].x << "}";
        }
        o << "]}";
    }
    o << "]";
    return o.str();
}                                                                         


}  // namespace

bool FloorMapStore_Apply(const std::string& line, std::string* reason) {
    const std::string b64 = jsonStr(line, "imageBase64");
    if (b64.empty()) {
        if (reason) *reason = "imageBase64 없음";
        return false;
    }

    std::string bytes;
    if (!base64Decode(b64, bytes)) {
        if (reason) *reason = "base64 디코드 실패";
        return false;
    }

    if (jsonStr(line, "imageFormat") == "png" && !looksLikePng(bytes)) {
        if (reason) *reason = "PNG 형식이 아님 (전송 중 잘렸을 수 있음)";
        return false;
    }

    std::ofstream f(FLOORMAP_IMAGE_PATH, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (reason) *reason = "이미지 저장 실패";
        return false;
    }
    f.write(bytes.data(), (std::streamsize)bytes.size());
    if (!f.good()) {
        if (reason) *reason = "이미지 쓰기 실패";
        return false;
    }
    f.close();
    std::cout << "[평면도] 이미지 저장 완료 (" << bytes.size() << " 바이트)\n";

    std::string why;                                                    
    std::string body = buildResultJson(FLOORMAP_IMAGE_PATH, why);
    if (body.empty()) {
        if (reason) *reason = why;
        return false;                          // 실패 시 기존 결과 유지
    }

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_resultJson = body;
    }
    std::ofstream jf(FLOORMAP_JSON_PATH, std::ios::trunc);
    if (jf) jf << "{" << body << "}";           // 파일은 온전한 JSON으로 저장
    else    std::cerr << "[평면도] 결과 파일 저장 실패 — 재시작 시 사라짐\n";

    std::cout << "[평면도] 변환 완료\n";
    return true;                                                        

}

std::string FloorMapStore_ToJson() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_resultJson;
}

void FloorMapStore_Load() {
    std::ifstream f(FLOORMAP_JSON_PATH);
    if (!f) return;                                 // 파일 없으면 빈 상태로 시작
    std::stringstream ss;
    ss << f.rdbuf();
    std::string all = ss.str();                                             
    // 파일은 {본문} 형태로 저장되지만, 메모리에는 본문만 둔다
    // (응답 만들 때 type·result 같은 다른 필드와 합쳐야 해서)
    if (all.size() >= 2 && all.front() == '{' && all.back() == '}')
        all = all.substr(1, all.size() - 2);
    std::lock_guard<std::mutex> lk(g_mtx);
    g_resultJson = all;    
    if (!g_resultJson.empty()) std::cout << "[평면도] 저장된 변환 결과 복원\n";
}