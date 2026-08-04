#include "query_handler.h"
#include "../json_util.h"
#include <sstream>
#include <iomanip>

// 한 번에 보낼 수 있는 최대 행 수. 초과 요청은 잘라서 응답
// (이벤트는 하루 수십~수백 건이라 충분. 부족해지면 페이지네이션으로 확장)
constexpr int MAX_ROWS = 500;

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
            << ",\"incidentId\":" << r.incidentId
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
    // TODO: "sensor_log" — 2단계 (집계 포함)

    return failed(reqId, "알 수 없는 target");
}