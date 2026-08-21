#include "floormap_store.h"
#include "../json_util.h"
#include "EvacPlanner.h" 

#include <array>
#include <cstdint>
#include <ctime> 
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

namespace {

std::mutex g_mtx;
std::string g_resultJson;   // 변환 결과 원문. 아직 없으면 빈 문자열
std::vector<EvacRoute> g_routes;   // 전광판 전송용 숫자 형태          
int g_displayCount = 0;   
std::string g_fileName;     // Qt가 올린 원래 파일명. 어느 평면도인지 확인용    
long g_uploadedAt = 0;      // 등록 시각 (서버가 찍음) 

// base64 글자 → 6비트 값. 표에 없는 글자는 -1
const std::array<int8_t, 256> kB64 = [] {
    std::array<int8_t, 256> t{};
    t.fill(-1);
    const char* set = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) t[(unsigned char)set[i]] = (int8_t)i;
    return t;
}();

// 어떤 평면도가 올라와 있는지 Qt에서 확인할 수 있게 이름·시각을 따로 남긴다.
// 변환 결과 파일에 못 넣는다 — 재시작 때 이미지로 변환을 다시 돌려 덮어쓴다
void saveMeta(const std::string& name, long ts) {
    std::ofstream f(FLOORMAP_META_PATH, std::ios::trunc);
    if (!f) { std::cerr << "[평면도] 등록 정보 저장 실패\n"; return; }
    f << "{\"fileName\":\"" << jsonEscape(name) << "\",\"uploadedAt\":" << ts << "}";
}

void loadMeta() {
    std::ifstream f(FLOORMAP_META_PATH);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string all = ss.str();
    g_fileName   = jsonStr(all, "fileName");
    g_uploadedAt = jsonLong(all, "uploadedAt", 0);
}                                                                             

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
std::string runConversion(const std::string& imagePath,           
                          std::vector<EvacRoute>& outRoutes, int& outDisplayCount,
                          std::string& reason) {                      
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
    // 같은 결과를 숫자 형태로도 남긴다 (전광판 전송용)                
    outDisplayCount = (int)displays.size();
    outRoutes.clear();
    outRoutes.reserve(routes.size());
    for (size_t i = 0; i < routes.size(); i++) {
        EvacRoute r;
        r.displayId = (int)(i / exits.size()) + 1;
        r.exitId    = (int)(i % exits.size()) + 1;
        r.waypoints = routes[i];
        outRoutes.push_back(std::move(r));
    }       
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

    // Qt가 파일명을 안 보내면 빈 값. 그때는 시각만으로 구분한다         
    const std::string name = jsonStr(line, "fileName");
    const long now = (long)std::time(nullptr);      

    std::string why;                                              
    std::vector<EvacRoute> routes;
    int displayCount = 0;
    std::string body = runConversion(FLOORMAP_IMAGE_PATH, routes, displayCount, why);
    if (body.empty()) {
        if (reason) *reason = why;
        return false;                          // 실패 시 기존 결과 유지
    }

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_resultJson   = body;
        g_routes       = std::move(routes);
        g_displayCount = displayCount;
        g_fileName     = name;                                
        g_uploadedAt   = now;
    }                                                            
    std::ofstream jf(FLOORMAP_JSON_PATH, std::ios::trunc);
    if (jf) jf << "{" << body << "}";           // 파일은 온전한 JSON으로 저장
    else    std::cerr << "[평면도] 결과 파일 저장 실패 — 재시작 시 사라짐\n";
    saveMeta(name, now);     

    std::cout << "[평면도] 변환 완료\n";
    return true;                                                        

}

std::string FloorMapStore_ToJson() {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_resultJson.empty()) return "";   // 등록 전이면 빈 값 — 호출부가 empty로 회신
    std::ostringstream o;
    o << g_resultJson
      << ",\"fileName\":\"" << jsonEscape(g_fileName) << "\""
      << ",\"uploadedAt\":" << g_uploadedAt;
    return o.str();
}

void FloorMapStore_Load() {     
    loadMeta();   // 이름·시각은 변환 성공 여부와 무관하게 먼저 복원                                           
    // 저장된 이미지로 변환을 다시 돌린다. 글자와 숫자를 한 경로에서 만들어야
    // 둘이 어긋나지 않는다 (재시작 후에도 전광판 전송이 되게)
    std::ifstream img(FLOORMAP_IMAGE_PATH, std::ios::binary);
    if (img) {
        img.close();
        std::vector<EvacRoute> routes;
        int displayCount = 0;
        std::string why;
        std::string body = runConversion(FLOORMAP_IMAGE_PATH, routes, displayCount, why);
        if (!body.empty()) {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_resultJson   = body;
            g_routes       = std::move(routes);
            g_displayCount = displayCount;
            std::cout << "[평면도] 저장된 이미지로 변환 복원 (전광판 "
                      << displayCount << "대, 경로 " << g_routes.size() << "개)\n";
            return;
        }
        std::cerr << "[평면도] 저장된 이미지 변환 실패 — " << why << "\n";
    }

    // 이미지가 없거나 변환이 안 되면 저장된 결과 글자만 복원한다.
    // Qt 화면은 나오지만 전광판 전송은 불가
    std::ifstream f(FLOORMAP_JSON_PATH);
    if (!f) return;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string all = ss.str();
    if (all.size() >= 2 && all.front() == '{' && all.back() == '}')
        all = all.substr(1, all.size() - 2);
    std::lock_guard<std::mutex> lk(g_mtx);
    g_resultJson = all;
    if (!g_resultJson.empty())
        std::cout << "[평면도] 결과만 복원 (전광판 전송 불가 — 이미지 없음)\n";
}                                                

bool FloorMapStore_HasRoutes() {                                     
    std::lock_guard<std::mutex> lk(g_mtx);
    return !g_routes.empty();
}

int FloorMapStore_DisplayCount() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_displayCount;
}

std::vector<EvacRoute> FloorMapStore_RoutesFor(int displayId) {
    std::vector<EvacRoute> out;
    std::lock_guard<std::mutex> lk(g_mtx);
    for (const auto& r : g_routes)
        if (r.displayId == displayId) out.push_back(r);
    return out;   // 출구 순서대로 담긴다 (g_routes 자체가 그 순서)
}                                                                  