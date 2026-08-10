#include "qt_link.h"
#include "db/query_handler.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <ctime>
#include <cstdlib>

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
    oss << ",\"evacuation\":" << (o.evacActive ? 1 : 0);   // 대피 모드 여부 <- 처음/끝
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
        << ",\"siren\":" << st.siren
        << ",\"fanSrc\":\""   << st.fanSrc   << "\""      // 자동/수동 구분 
        << ",\"valveSrc\":\"" << st.valveSrc << "\""
        << ",\"sirenSrc\":\"" << st.sirenSrc << "\""
        << ",\"link\":\"" << (st.linkOk ? "ok" : "down") << "\""   // STM 연결 상태
        << "}";                                                  
    link.send(oss.str());
}

// 수동 제어 처리 + control_ack 응답
static void handleControl(Link& link, Database& db, const std::string& line) {
    std::string cmdId  = jsonStr(line, "cmdId");
    std::string zone   = jsonStr(line, "zone");
    std::string target = jsonStr(line, "target");
    std::string action = jsonStr(line, "action");

    std::string reason;                                       // 실패 사유 (STM쪽에서 채움)
    bool ok = Actuator_Execute(target, action, "수동", &reason);
    if (!ok) {                                                  
        std::cerr << "[제어] 수동 실패 — " << target << ":" << action
                  << " (" << reason << ")\n";
        Actuator_Poll();   // 링크가 죽은 건지 이 명령만 실패한 건지 즉시 확인
    }                                                           
    QtLink_SendActuator(link, Actuator_GetState());   // 실행 결과 → Qt 화면 갱신

    // 실패한 명령도 남긴다. 성공으로만 기록하면 나중에 추적이 안 됨
    db.insertEvent(std::time(nullptr), zone, "manual_control", "", "",
                   "", "manual", target + ":" + action, "admin",
                   0, 0, ok ? "" : "실패", 0, "", 0, reason);

    // 명세서 control_ack 규격: cmdId 반사
    std::ostringstream oss;
    oss << "{\"type\":\"control_ack\",\"cmdId\":\"" << cmdId
        << "\",\"zone\":\"" << zone << "\",\"target\":\"" << target
        << "\",\"result\":\"" << (ok ? "ok" : "failed") << "\""
        << ",\"reason\":" << (ok ? "null" : "\"" + jsonEscape(reason) + "\"")
        << ",\"ts\":" << std::time(nullptr) << "}";
    link.send(oss.str());
}

// 수동 대피 모드 발동/해제. 실제 실행은 sensorWorker가 다음 tick에 한다
// (액추에이터·전광판을 만지는 코드를 한 곳에 유지하기 위함)
static void handleEvacuation(Link& link, Database& db, AlarmState& alarm,
                             const std::string& line, bool on) {
    std::string cmdId = jsonStr(line, "cmdId");
    std::string zone  = jsonStr(line, "zone");    // 발생 구역 표시용. 적용은 전 구역
    std::string admin = jsonStr(line, "admin");

    alarm.onEvacuationRequest(on);
    std::cout << "[대피] " << (on ? "발동" : "해제") << " 요청 — " << admin << "\n";

    db.insertEvent(std::time(nullptr), zone,
                   on ? "evacuation" : "evacuation_clear", "", "",
                   "", "manual", on ? "대피 모드 발동" : "대피 모드 해제", admin,
                   0, 0, "", 0, "", 0, "");

    std::ostringstream oss;
    oss << "{\"type\":\"evacuation_ack\",\"cmdId\":\"" << cmdId
        << "\",\"zone\":\"" << zone << "\",\"mode\":\"" << (on ? "trigger" : "clear")
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
        else if (line.find("\"type\":\"evacuation_trigger\"") != std::string::npos)  
            handleEvacuation(link, db, alarm, line, true);
        else if (line.find("\"type\":\"evacuation_clear\"") != std::string::npos)
            handleEvacuation(link, db, alarm, line, false);                         
        else if (line.find("\"type\":\"query\"") != std::string::npos)   
            link.send(handleQuery(db, line));
    }
}