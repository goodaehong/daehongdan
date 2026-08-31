// query_handler.h 구현. 조회 종류별로 SQL 을 고르고 응답 JSON 을 조립한다.

#include "query_handler.h"
#include "../json_util.h"
#include <sstream>
#include <iomanip>

// 한 번에 보낼 수 있는 최대 행 수. 초과 요청은 잘라서 응답
// (이벤트는 하루 수십~수백 건이라 충분. 부족해지면 페이지네이션으로 확장)
constexpr int MAX_ROWS = 500;

// 그래프 한 화면에 보낼 최대 점 수. 이 아래로 떨어지는 가장 작은 단위를 고른다  
constexpr int MAX_POINTS = 4000;

// 조회 범위 → 집계 단위(초). 명세서 3-2 집계 규칙
//   10분·1시간 = 원본(1) / 6시간 = 10초 / 하루 = 60초
//   그보다 넓은 범위도 점 개수가 넘치지 않게 5분·1시간 단위를 예비로 둠
static int pickBucket(long rangeSec) {
    static const int CANDIDATES[] = { 1, 10, 60, 300, 3600 };
    for (int b : CANDIDATES)
        if (rangeSec / b <= MAX_POINTS) return b;
    return 3600;
}                                                                            

// 명세서 3-3. 실패 응답
static std::string failed(const std::string& reqId, const std::string& reason) {
    std::ostringstream oss;
    oss << "{\"type\":\"query_result\",\"reqId\":\"" << jsonEscape(reqId)
        << "\",\"result\":\"failed\",\"reason\":\"" << jsonEscape(reason) << "\"}";
    return oss.str();
}

// 명세서 3-1. 이벤트 로그 조회
static std::string queryEventLog(Database& db, const std::string& reqId,
                                 long from, long to, int limit) {
    std::vector<EventRow> rows = db.queryEvents(from, to, limit);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << "{\"type\":\"query_result\",\"reqId\":\"" << jsonEscape(reqId)
        << "\",\"target\":\"event_log\",\"count\":" << rows.size() << ",\"rows\":[";

    for (size_t i = 0; i < rows.size(); ++i) {
        const EventRow& r = rows[i];
        if (i > 0) oss << ",";
        oss << "{\"id\":" << r.id
            << ",\"ts\":" << r.ts
            << ",\"zone\":\""        << jsonEscape(r.zone)        << "\""
            << ",\"category\":\""    << jsonEscape(r.category)    << "\""
            << ",\"severity\":\""    << jsonEscape(r.severity)    << "\""
            << ",\"cause\":\""       << jsonEscape(r.cause)       << "\""
            << ",\"sensorCombo\":\"" << jsonEscape(r.sensorCombo) << "\""
            << ",\"source\":\""      << jsonEscape(r.source)      << "\""
            << ",\"response\":\""    << jsonEscape(r.response)    << "\""
            << ",\"admin\":\""       << jsonEscape(r.admin)       << "\""
            << ",\"gasPpm\":"   << r.gasPpm
            << ",\"smokePpm\":" << r.smokePpm
            << ",\"status\":\""       << jsonEscape(r.status)       << "\""
            << ",\"durationMs\":" << r.durationMs
            << ",\"snapshotPath\":\"" << jsonEscape(r.snapshotPath) << "\""
            << ",\"clipPath\":\"" << jsonEscape(r.clipPath) << "\"" 
            << ",\"incidentId\":" << r.incidentId
            << "}";
    }
    oss << "]}";
    return oss.str();
}

// 명세서 3-2. 센서 이력 조회
static std::string querySensorLog(Database& db, const std::string& reqId,
                                  long from, long to, const std::string& zone) {
    int bucket = pickBucket(to - from);
    std::vector<SensorPoint> pts = db.querySensors(from, to, zone, bucket);

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << "{\"type\":\"query_result\",\"reqId\":\"" << jsonEscape(reqId)
        << "\",\"target\":\"sensor_log\",\"bucketSec\":" << bucket
        << ",\"count\":" << pts.size() << ",\"rows\":[";

    for (size_t i = 0; i < pts.size(); ++i) {
        const SensorPoint& p = pts[i];
        if (i > 0) oss << ",";
        oss << "{\"t\":" << p.t
            << ",\"gasAvg\":"   << p.gasAvg
            << ",\"gasMax\":"   << p.gasMax
            << ",\"smokeAvg\":" << p.smokeAvg
            << ",\"smokeMax\":" << p.smokeMax
            << "}";
    }
    oss << "]}";
    return oss.str();
}

std::string handleQuery(Database& db, const std::string& line) {
    std::string reqId  = jsonStr(line, "reqId");
    std::string target = jsonStr(line, "target");
    long from = jsonLong(line, "from", 0);
    long to   = jsonLong(line, "to",   0);
    int  limit = jsonInt(line, "limit", 100);

    if (from <= 0 || to <= 0 || from > to) return failed(reqId, "잘못된 기간");
    if (limit <= 0 || limit > MAX_ROWS) limit = MAX_ROWS;   // 상한 강제
    

    if (target == "event_log")  return queryEventLog(db, reqId, from, to, limit);
    if (target == "sensor_log") {                                     
        std::string zone = jsonStr(line, "zone");
        if (zone.empty()) zone = "A";        // 명세: 생략 시 "A"
        return querySensorLog(db, reqId, from, to, zone);
    }      

    return failed(reqId, "알 수 없는 target");
}