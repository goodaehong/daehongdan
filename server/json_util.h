#pragma once
#include <string>
#include <vector>
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

// 키의 값을 원문 그대로 잘라낸다. 배열·객체는 괄호 짝을 맞춰 통째로.       
// 문자열 안의 괄호는 세지 않는다
inline std::string jsonRaw(const std::string& j, const std::string& key) {
    std::string pat = "\"" + key + "\":";
    size_t s = j.find(pat);
    if (s == std::string::npos) return "";
    s += pat.size();
    while (s < j.size() && (j[s] == ' ' || j[s] == '\t' || j[s] == '\n')) s++;
    if (s >= j.size()) return "";

    char open = j[s];
    if (open != '[' && open != '{') {              // 스칼라 → , 나 } 까지
        size_t e = j.find_first_of(",}", s);
        return j.substr(s, (e == std::string::npos ? j.size() : e) - s);
    }
    char close = (open == '[') ? ']' : '}';
    int depth = 0;
    bool inStr = false;
    for (size_t i = s; i < j.size(); i++) {
        char c = j[i];
        if (inStr) {
            if      (c == '\\') i++;                // 이스케이프 다음 글자 건너뜀
            else if (c == '"')  inStr = false;
            continue;
        }
        if      (c == '"')   inStr = true;
        else if (c == open)  depth++;
        else if (c == close && --depth == 0) return j.substr(s, i - s + 1);
    }
    return "";                                      // 짝이 안 맞음
}

// 배열 원문("[a,b,c]")을 최상위 요소들로 쪼갠다. 중첩 괄호·문자열은 건너뜀
inline std::vector<std::string> jsonArray(const std::string& arr) {
    std::vector<std::string> out;
    if (arr.size() < 2) return out;
    size_t end = arr.size() - 1;                    // 마지막 ] 제외
    int depth = 0;
    bool inStr = false;
    size_t start = 1;
    for (size_t i = 1; i <= end; i++) {
        char c = (i < end) ? arr[i] : ',';          // 끝에서 마지막 요소 밀어냄
        if (inStr) {
            if      (c == '\\') i++;
            else if (c == '"')  inStr = false;
            continue;
        }
        if      (c == '"') inStr = true;
        else if (c == '[' || c == '{') depth++;
        else if (c == ']' || c == '}') depth--;
        else if (c == ',' && depth == 0) {
            std::string e = arr.substr(start, i - start);
            size_t a = e.find_first_not_of(" \t\n");
            size_t b = e.find_last_not_of(" \t\n");
            if (a != std::string::npos) out.push_back(e.substr(a, b - a + 1));
            start = i + 1;
        }
    }
    return out;
}

inline bool jsonBool(const std::string& j, const std::string& key, bool def) {
    std::string v = jsonRaw(j, key);
    return v.empty() ? def : (v.find("true") != std::string::npos);
}

inline double jsonDouble(const std::string& j, const std::string& key, double def) {
    std::string v = jsonRaw(j, key);
    return v.empty() ? def : std::atof(v.c_str());
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