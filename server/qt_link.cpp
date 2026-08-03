#include "qt_link.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdlib>

std::string jsonStr(const std::string& j, const std::string& key) {
    std::string pat = "\"" + key + "\":\"";
    size_t s = j.find(pat);
    if (s == std::string::npos) return "";
    s += pat.size();
    size_t e = j.find('"', s);
    return (e == std::string::npos) ? "" : j.substr(s, e - s);
}

int jsonInt(const std::string& j, const std::string& key, int def) {
    std::string pat = "\"" + key + "\":";
    size_t s = j.find(pat);
    if (s == std::string::npos) return def;
    return std::atoi(j.c_str() + s + pat.size());
}

// ── 센서 정보 ──
void QtLink_SendSensor(Link& link, const SensorReading& s, const AlarmOutcome& o) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << "{\"type\":\"sensor\",\"zone\":\"A\""
        << ",\"ts\":" << std::time(nullptr)
        << ",\"temp\":" << s.temp
        << ",\"humidity\":" << s.humidity
        << ",\"gasPpm\":" << s.gasPpm
        << ",\"smokePpm\":" << s.smokePpm
        << ",\"flameVal\":" << s.flameVal
        << ",\"state\":\"" << o.j.state << "\""
        << ",\"cause\":\"" << o.j.cause << "\"";
    if (o.j.state == "warning") oss << ",\"warnRemain\":" << o.warnRemain;
    oss << "}";
    link.send(oss.str());
}

// ── 카메라 정보 (화재 + 연기 박스) ──
void QtLink_SendDetection(Link& link, int ch, int frameId, int srcW, int srcH,
                          bool alarm, const std::vector<DetBox>& boxes) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{\"type\":\"detection\""
        << ",\"channel\":" << (ch + 1)
        << ",\"frameId\":" << frameId
        << ",\"srcW\":" << srcW
        << ",\"srcH\":" << srcH
        << ",\"alarm\":" << (alarm ? "true" : "false")
        << ",\"boxes\":[";

    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto& b = boxes[i];
        if (i > 0) oss << ",";
        oss << "{\"x\":" << b.x << ",\"y\":" << b.y
            << ",\"w\":" << b.w << ",\"h\":" << b.h
            << ",\"cls\":\"" << b.cls << "\""
            << ",\"score\":" << b.score << "}";
    }
    oss << "]}";
    link.send(oss.str());
}

// ── 사람 감지 ──
// count는 박스 개수. Qt가 score로 한 번 더 거르면 표시 인원은 달라질 수 있음
void QtLink_SendPerson(Link& link, int ch, int srcW, int srcH,
                       const std::vector<PersonBox>& persons) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "{\"type\":\"person\""
        << ",\"channel\":" << (ch + 1)
        << ",\"srcW\":" << srcW
        << ",\"srcH\":" << srcH
        << ",\"count\":" << persons.size()
        << ",\"boxes\":[";

    for (size_t i = 0; i < persons.size(); ++i) {
        const auto& p = persons[i];
        if (i > 0) oss << ",";
        oss << "{\"x\":" << p.x << ",\"y\":" << p.y
            << ",\"w\":" << p.w << ",\"h\":" << p.h
            << ",\"score\":" << p.score << "}";
    }
    oss << "]}";
    link.send(oss.str());
}

// ── 액추에이터 상태 ──
void QtLink_SendActuator(Link& link, const ActuatorSnapshot& st) {
    std::ostringstream oss;
    oss << "{\"type\":\"actuator_status\",\"fan\":" << st.fan
        << ",\"valve\":" << st.valve
        << ",\"siren\":" << st.siren << "}";
    link.send(oss.str());
}

// 수동 제어 처리 + control_ack 응답
static void handleControl(Link& link, Database& db, const std::string& line) {
    std::string cmdId  = jsonStr(line, "cmdId");
    std::string zone   = jsonStr(line, "zone");
    std::string target = jsonStr(line, "target");
    std::string action = jsonStr(line, "action");

    Actuator_Execute(target, action, "수동");
    QtLink_SendActuator(link, Actuator_GetState());   // 실행 결과 → Qt 화면 갱신

    db.insertEvent(std::time(nullptr), zone, "manual_control", "", "",
                   "", "manual", target + ":" + action, "admin",
                   0, 0, "", 0, "", 0, "");

    // 명세서 control_ack 규격: cmdId 반사, 지금은 무조건 ok (STM 없으니 실패할 게 없음)
    std::ostringstream oss;
    oss << "{\"type\":\"control_ack\",\"cmdId\":\"" << cmdId
        << "\",\"zone\":\"" << zone << "\",\"target\":\"" << target
        << "\",\"result\":\"ok\",\"reason\":null,\"ts\":" << std::time(nullptr) << "}";
    link.send(oss.str());
}

// ── 수신 스레드: Qt→서버 방향 ──
// \n 프레이밍·대기는 Link가 처리하므로 여기선 한 줄씩 받아 라우팅만
void QtLink_RecvWorker(Link& link, AlarmState& alarm, Database& db) {
    std::string line;
    while (link.recvLine(line)) {
        if (line.find("\"type\":\"warning_ack\"") != std::string::npos)
            alarm.onWarningAck();          // 관리자 인지 → 센서 스레드가 타이머 취소
        else if (line.find("\"type\":\"control\"") != std::string::npos)
            handleControl(link, db, line);
        // TODO: "query" → DB 조회 프로토콜
    }
}