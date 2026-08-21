#include "qt_link.h"
#include "db/query_handler.h"
#include "roi/roi_store.h"   
#include "floormap/floormap_store.h"  
#include "audio/speaker_alert.h" 
#include "clip/clip_recorder.h" 
#include "calib/calib_store.h" 
#include <sstream>
#include <iomanip>
#include <iostream>
#include <ctime>
#include <cstdlib>
#include <mutex>

// 서버가 내린 목표 대응. 센서 스레드가 쓰고 두 스레드가 읽는다        
static std::mutex g_targetMtx;
static Response   g_target = responseForSafe();

void QtLink_SetTarget(const Response& r) {
    std::lock_guard<std::mutex> lk(g_targetMtx);
    g_target = r;
}
Response QtLink_GetTarget() {
    std::lock_guard<std::mutex> lk(g_targetMtx);
    return g_target;
}

// "checklist":[...] 원문을 통째로 꺼낸다 (배열이라 jsonStr로는 못 읽음)
static std::string jsonRawArray(const std::string& line, const std::string& key) {
    auto p = line.find("\"" + key + "\"");
    if (p == std::string::npos) return "";
    auto b = line.find('[', p);
    auto e = line.find(']', b);
    if (b == std::string::npos || e == std::string::npos) return "";
    return line.substr(b, e - b + 1);
}                                                                

// ── 센서 정보 ──
void QtLink_SendSensor(Link& link, const SensorReading& s, const AlarmOutcome& o,const ServerStatus& st) {
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
    
    // 감지·대응이 살아있는가 + 해제 체크리스트 (매초 보냄 — Qt가 필드 유무를 분기 안 하게) 
    oss << ",\"sensorOk\":"      << (st.sensorOk   ? "true" : "false")
        << ",\"dhtOk\":"         << (s.dhtOk       ? "true" : "false") 
        << ",\"responseOk\":"    << (st.responseOk ? "true" : "false")
        << ",\"dangerSource\":\"" << (o.manual ? "manual" : "auto") << "\""
        << ",\"admin\":\""        << jsonEscape(o.admin) << "\""
        << ",\"visionOk\":[";
    for (int i = 0; i < 4; i++)
        oss << (i ? "," : "") << (st.visionOk[i] ? "true" : "false");
    oss << "],\"clearCheck\":{\"sensor\":" << (st.clearSensor   ? "true" : "false")
        << ",\"vision\":"                  << (st.clearVision   ? "true" : "false")
        << ",\"actuator\":"                << (st.clearActuator ? "true" : "false") << "}";

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
    Response t = QtLink_GetTarget(); 
    std::ostringstream oss;
    oss << "{\"type\":\"actuator_status\",\"fan\":" << st.fan
        << ",\"valve\":" << st.valve
        << ",\"siren\":" << st.siren
        << ",\"fanSrc\":\""   << st.fanSrc   << "\""      // 자동/수동 구분 
        << ",\"valveSrc\":\"" << st.valveSrc << "\""
        << ",\"sirenSrc\":\"" << st.sirenSrc << "\""
        << ",\"link\":\"" << (st.linkOk ? "ok" : "down") << "\""   // STM 연결 상태
        << ",\"linkReason\":\"" << jsonEscape(st.linkReason) << "\""     
        << ",\"voice\":" << (SpeakerAlert_IsActive() ? 1 : 0)   // 대피 음성 (사이렌과 별개)  
        << ",\"target\":{\"fan\":" << t.fan << ",\"valve\":" << t.valve
        << ",\"siren\":" << t.siren << "}"
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
    bool ok;                                                            
    if (target == "speaker") {
        // 대피 안내 음성은 STM이 아니라 서버가 직접 트는 WAV라 별도 경로.
        // 지금 재생 중인 것만 끈다 — 다음 위험 진입/재실행 때 sensorWorker가
        // SpeakerAlert_Start()를 다시 부르면 자연스럽게 재생된다
        SpeakerAlert_Stop();
        ok = true;
    } else {
        ok = Actuator_Execute(target, action, "수동", &reason);
        if (!ok) {                                                  
            std::cerr << "[제어] 수동 실패 — " << target << ":" << action
                      << " (" << reason << ")\n";
            Actuator_Poll();   // 링크가 죽은 건지 이 명령만 실패한 건지 즉시 확인
        }
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

// 비상 모드 전환/해제. 실제 실행은 sensorWorker가 다음 tick에 한다
// (액추에이터·전광판을 만지는 코드를 한 곳에 유지하기 위함)
static void handleEmergency(Link& link, Database& db, AlarmState& alarm,
                            const std::string& line, bool on) {
    std::string cmdId = jsonStr(line, "cmdId");
    std::string zone  = jsonStr(line, "zone");    // 발생 구역 표시용. 적용은 전 구역
    std::string admin = jsonStr(line, "admin");
    std::string cause = jsonStr(line, "cause");   // 전환일 때만. 해제는 빈 값

    alarm.requestEmergency(on, cause, admin);
    std::cout << "[비상] " << (on ? "전환" : "해제") << " 요청 — " << admin
              << (on ? " (" + cause + ")" : "") << "\n";

    // 해제는 확인자·체크 내역을 남긴다. 사태 종료 기록(resolve)은 센서 스레드가 따로 남김
    if (!on)
        db.insertEvent(std::time(nullptr), zone, "emergency_clear", "", "", "",
                       "manual", "비상 모드 해제", admin,
                       0, 0, "", 0, "", 0, jsonRawArray(line, "checklist"));

    // 거절 없음 — 켜는 방향은 막지 않고, 해제도 "해제 후 재발"이라 접수로 답한다
    std::ostringstream oss;
    oss << "{\"type\":\"emergency_ack\",\"cmdId\":\"" << cmdId
        << "\",\"zone\":\"" << zone << "\",\"mode\":\"" << (on ? "trigger" : "clear")
        << "\",\"result\":\"accepted\",\"ts\":" << std::time(nullptr) << "}";
    link.send(oss.str());
}

// ROI 설정 반영 + set_ignore_regions_ack 응답                       
// 검증 실패 시 기존 설정을 유지한다 (RoiStore_Apply 안에서 처리)
static void handleSetIgnoreRegions(Link& link, const std::string& line) {
    std::string cmdId = jsonStr(line, "cmdId");
    int ch = 0;
    std::string reason;
    bool ok = RoiStore_Apply(line, &ch, &reason);

    // 메모리엔 반영됐어도 파일 저장이 실패하면 재시작 때 사라진다 → Qt에 알려야 함
    if (ok && !RoiStore_Save()) {
        ok = false;
        reason = "설정 파일 저장 실패";
    }
    if (!ok) std::cerr << "[ROI] ch" << ch << " 적용 실패 — " << reason << "\n";

    std::ostringstream oss;
    oss << "{\"type\":\"set_ignore_regions_ack\",\"cmdId\":\"" << cmdId
        << "\",\"channel\":" << ch
        << ",\"result\":\"" << (ok ? "ok" : "failed") << "\""
        << ",\"reason\":" << (ok ? "null" : "\"" + jsonEscape(reason) + "\"")
        << ",\"ts\":" << std::time(nullptr) << "}";
    link.send(oss.str());
}

// query_result 본문. reqId가 비면 접속 직후 push용 (reqId 없이 나간다)
static std::string ignoreRegionsResult(int ch, const std::string& reqId) {
    if (ch < 0 || ch > 3) ch = 0;
    std::ostringstream oss;
    oss << "{\"type\":\"query_result\"";
    if (!reqId.empty()) oss << ",\"reqId\":\"" << reqId << "\"";
    oss << ",\"target\":\"ignore_regions\",\"channel\":" << (ch + 1)
        << ",\"overlapThreshold\":" << RoiStore_Threshold(ch)
        << ",\"regions\":" << RoiStore_ToJson(ch) << "}";
    return oss.str();
}

// Qt 접속 직후 4채널 전부 전송 (Q3 회신 — query 대신 서버 push)
void QtLink_PushIgnoreRegions(Link& link) {
    for (int ch = 0; ch < 4; ch++) link.send(ignoreRegionsResult(ch, ""));
}                                                                

// 평면도 업로드 처리 + floor_map_result 응답                            
// 지금은 이미지 저장까지만 되고 변환은 미연동이라 failed가 나간다
static void handleSetFloorMap(Link& link, const std::string& line) {
    std::string cmdId = jsonStr(line, "cmdId");
    std::string reason;
    bool ok = FloorMapStore_Apply(line, &reason);
    if (!ok) std::cerr << "[평면도] 처리 실패 — " << reason << "\n";

    std::ostringstream oss;
    oss << "{\"type\":\"floor_map_result\",\"cmdId\":\"" << cmdId
        << "\",\"result\":\"" << (ok ? "ok" : "failed") << "\""
        << ",\"reason\":" << (ok ? "null" : "\"" + jsonEscape(reason) + "\"");
    if (ok) oss << "," << FloorMapStore_ToJson();   // gridSize·bitmap·displays·exits·routes
    oss << ",\"ts\":" << std::time(nullptr) << "}";
    link.send(oss.str());
}

// query target="floor_map" 응답. 저장된 결과가 없으면 result:"empty"
static std::string floorMapResult(const std::string& reqId) {
    std::string body = FloorMapStore_ToJson();
    std::ostringstream oss;
    oss << "{\"type\":\"query_result\",\"reqId\":\"" << reqId
        << "\",\"target\":\"floor_map\"";
    if (body.empty()) oss << ",\"result\":\"empty\"";
    else              oss << ",\"result\":\"ok\"," << body;
    oss << "}";
    return oss.str();
}                                                                        

// query target="clip" 응답. mp4를 base64로 실어 보낸다.                 
// 로그 상세에서 재생 버튼을 눌렀을 때만 오는 요청 — 목록 조회엔 안 실린다
static std::string clipResult(const std::string& reqId, const std::string& path) {
    std::ostringstream oss;
    oss << "{\"type\":\"query_result\",\"reqId\":\"" << jsonEscape(reqId)
        << "\",\"target\":\"clip\"";

    // 경로는 Qt가 보낸 값이다. 그대로 열면 서버의 아무 파일이나 새어나간다.
    // 서버가 만든 클립 폴더 안인지만 확인한다
    const std::string dir = CLIP_DIR;
    if (path.size() <= dir.size() || path.compare(0, dir.size(), dir) != 0
        || path.find("..") != std::string::npos) {
        oss << ",\"result\":\"error\",\"reason\":\"허용되지 않은 경로\"}";
        return oss.str();
    }

    std::string data = ClipRecorder_ReadBase64(path);
    if (data.empty()) oss << ",\"result\":\"empty\"";
    else oss << ",\"result\":\"ok\",\"format\":\"mp4\""
             << ",\"path\":\"" << jsonEscape(path) << "\""
             << ",\"data\":\"" << data << "\"";
    oss << "}";
    return oss.str();
}                                  

// query target="snapshot" 응답. jpg를 base64로 실어 보낸다.             
// 경로만 보내면 Qt(윈도우)가 파이 안의 파일을 못 연다 — 클립과 같은 이유
static std::string snapshotResult(const std::string& reqId, const std::string& path) {
    std::ostringstream oss;
    oss << "{\"type\":\"query_result\",\"reqId\":\"" << jsonEscape(reqId)
        << "\",\"target\":\"snapshot\"";

    // 경로는 Qt가 보낸 값이다. 그대로 열면 서버의 아무 파일이나 새어나간다
    const std::string dir = SNAPSHOT_DIR;
    if (path.size() <= dir.size() || path.compare(0, dir.size(), dir) != 0
        || path.find("..") != std::string::npos) {
        oss << ",\"result\":\"error\",\"reason\":\"허용되지 않은 경로\"}";
        return oss.str();
    }

    std::string data = ClipRecorder_ReadBase64(path);   // 파일 → base64 (형식 무관)
    if (data.empty()) oss << ",\"result\":\"empty\"";
    else oss << ",\"result\":\"ok\",\"format\":\"jpg\""
             << ",\"path\":\"" << jsonEscape(path) << "\""
             << ",\"data\":\"" << data << "\"";
    oss << "}";
    return oss.str();
}                                                                     

// query target="calib_status" 응답. 4채널 보정 단계 + 실시간 마커 수·오차     
static std::string calibStatusResult(const std::string& reqId) {
    std::ostringstream oss;
    oss << "{\"type\":\"query_result\",\"reqId\":\"" << jsonEscape(reqId)
        << "\",\"target\":\"calib_status\",\"result\":\"ok\","
        << CalibStore_ToJson()
        << ",\"ts\":" << std::time(nullptr) << "}";
    return oss.str();
}

// 보정 파일 재로드 요청. 실제 로드는 해당 채널 워커가 다음 프레임에 수행한다.
// 여기서는 "접수했다"만 답하고, 결과는 Qt가 calib_status로 다시 조회한다
static void handleReloadCalibration(Link& link, const std::string& line) {
    int ch = jsonInt(line, "channel", 0) - 1;
    std::ostringstream oss;
    oss << "{\"type\":\"reload_calibration_result\",\"channel\":" << (ch + 1);
    if (ch < 0 || ch >= 4) {
        oss << ",\"result\":\"error\",\"reason\":\"채널 범위 초과\"";
    } else {
        CalibStore_RequestReload(ch);
        std::cout << "[좌표] cam" << ch + 1 << " 재로드 요청 접수\n";
        oss << ",\"result\":\"accepted\"";
    }
    oss << ",\"ts\":" << std::time(nullptr) << "}";
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
        else if (line.find("\"type\":\"emergency_trigger\"") != std::string::npos) 
            handleEmergency(link, db, alarm, line, true);
        else if (line.find("\"type\":\"emergency_clear\"") != std::string::npos)
            handleEmergency(link, db, alarm, line, false);                                               
        else if (line.find("\"type\":\"set_ignore_regions\"") != std::string::npos)   
            handleSetIgnoreRegions(link, line);
        else if (line.find("\"type\":\"set_floor_map\"") != std::string::npos)        
            handleSetFloorMap(link, line);                    
        else if (line.find("\"type\":\"reload_calibration\"") != std::string::npos)   
            handleReloadCalibration(link, line);                                                          
        else if (line.find("\"type\":\"query\"") != std::string::npos) {
            if (jsonStr(line, "target") == "ignore_regions")
                link.send(ignoreRegionsResult(jsonInt(line, "channel", 1) - 1,
                                              jsonStr(line, "reqId")));
            else if (jsonStr(line, "target") == "floor_map")                         
                link.send(floorMapResult(jsonStr(line, "reqId"))); 
            else if (jsonStr(line, "target") == "clip")                 
                link.send(clipResult(jsonStr(line, "reqId"),
                                     jsonStr(line, "path")));    
            else if (jsonStr(line, "target") == "calib_status")               
                link.send(calibStatusResult(jsonStr(line, "reqId")));       
            else if (jsonStr(line, "target") == "snapshot")                   
                link.send(snapshotResult(jsonStr(line, "reqId"),
                                         jsonStr(line, "path")));                      

            else
                link.send(handleQuery(db, line));
        }                                                                           
    }
}