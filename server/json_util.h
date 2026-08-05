#pragma once
#include <string>
#include <cstdlib>

// JSON 필드 추출 헬퍼 (라이브러리 없이 문자열 검색으로)
inline std::string jsonStr(const std::string& j, const std::string& key) {
    std::string pat = "\"" + key + "\":\"";
    size_t s = j.find(pat);
    if (s == std::string::npos) return "";
    s += pat.size();
    size_t e = j.find('"', s);
    return (e == std::string::npos) ? "" : j.substr(s, e - s);
}

inline long jsonLong(const std::string& j, const std::string& key, long def) {
    std::string pat = "\"" + key + "\":";
    size_t s = j.find(pat);
    if (s == std::string::npos) return def;
    return std::atol(j.c_str() + s + pat.size());
}

inline int jsonInt(const std::string& j, const std::string& key, int def) {
    return (int)jsonLong(j, key, def);
}

// JSON 문자열 값에 들어갈 수 없는 문자 이스케이프 (경로의 역슬래시, 따옴표 등)
inline std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if      (c == '"')  out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}