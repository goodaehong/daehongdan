#include "floormap_store.h"
#include "../json_util.h"

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

    // TODO: EvacPlanner 머지되면 여기서 변환 실행 → g_resultJson 채우고 파일 저장
    if (reason) *reason = "변환 모듈 미연동 (이미지는 저장됨)";
    return false;
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
    std::lock_guard<std::mutex> lk(g_mtx);
    g_resultJson = ss.str();
    if (!g_resultJson.empty()) std::cout << "[평면도] 저장된 변환 결과 복원\n";
}