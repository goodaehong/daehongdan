// control_loop.h 구현. 1초 주기로 판정 · 대응 · 기록 · 전송을 순서대로 실행한다.

#include "control_loop.h"

#include <opencv2/opencv.hpp>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#include "../drivers/stm_uart_display/stm_display_protocol.h"
#include "actuator/actuator_control.h"
#include "audio/speaker_alert.h"
#include "clip/clip_recorder.h"
#include "display/stm_display.h"
#include "floormap/floormap_store.h"
#include <mutex>
#include "judgement.h"
#include "qt_link.h"

// 감지 프레임을 jpg로 저장 → 경로 반환. 실패 시 빈 문자열
std::string saveSnapshot(FrameStore& store, int ch, const std::string& zone, long ts) {
    if (ch < 0) return "";
    cv::Mat frame;
    {
        std::lock_guard<std::mutex> lock(store.mtx[ch]);
        if (store.frames[ch].empty()) return "";
        frame = store.frames[ch].clone();
    }
    
    const std::string dir = SNAPSHOT_DIR;   // 실행 위치와 무관하게 고정 <- 처음
    ::mkdir(dir.c_str(), 0755); 
    std::string path = dir + "/" + zone + "_" + std::to_string(ts)
                     + "_cam" + std::to_string(ch + 1) + ".jpg";
    if (!cv::imwrite(path, frame)) return "";
    return path;
}

// ── 판단 결과 → 출력 모듈 입력으로 변환 ──
// 출력 모듈(전광판·액추에이터)이 판단 쪽 타입을 알지 않도록 배선하는 쪽에서 바꿔 넘긴다
ActuatorCommand toActuatorCommand(const Response& r) {
    return ActuatorCommand{ r.fan, r.valve, r.siren };
}

static DisplayUpdate toDisplayUpdate(const SensorReading& s) {
    return DisplayUpdate{ gasLevel(s.gasPpm), s.gasPpm, s.temp, s.humidity };
}

// Cause::Gas 만 가스 화면, 나머지(화재·연기·복합 원인)는 전부 화재 화면 (팀 확인 완료)
static DisplayDisaster toDisaster(const std::string& cause) {
    return (cause == Cause::Gas) ? DisplayDisaster::Gas : DisplayDisaster::Fire;
}

// Response → event_log에 남길 대응 내역 문자열
static std::string respToText(const Response& r) {
    const char* fan = (r.fan == 0) ? "off" : (r.fan == 1) ? "low"
                    : (r.fan == 2) ? "mid" : "high";
    return std::string("siren_") + (r.siren ? "on" : "off")
         + ",valve_" + (r.valve ? "open" : "close")
         + ",fan_"  + fan;
}

// 목표가 실제 액추에이터에 반영됐는가
// 관리자가 수동으로 조작한 장치는 목표와 달라도 정상 → 비교 제외
static bool responseApplied(const Response& t, const ActuatorSnapshot& a) {
    auto isAuto = [](const std::string& src) { return src != "manual"; };  
    if (!a.linkOk) return false;
    if (isAuto(a.fanSrc)   && a.fan   != t.fan)   return false;
    if (isAuto(a.valveSrc) && a.valve != t.valve) return false;
    if (isAuto(a.sirenSrc) && a.siren != t.siren) return false;
    return true;
}                                                                   

// 4채널의 화재 좌표를 한 목록으로 모은다. 전광판 표시와 경로 필터링 둘 다    
// 화재 전부를 봐야 해서, 채널을 가리지 않고 합친다
static std::vector<FireCell> collectFires() {
    std::vector<FireCell> all;
    for (int ch = 0; ch < 4; ch++) {
        std::lock_guard<std::mutex> lk(detState[ch].fireMtx);
        all.insert(all.end(), detState[ch].fires.begin(), detState[ch].fires.end());
    }
    return all;
}

// 목록이 같은지 비교. 순서까지 같아야 같은 것으로 본다
// (채널 순서로 모으므로 같은 상황이면 순서도 같다)
static bool sameFires(const std::vector<FireCell>& a, const std::vector<FireCell>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].radius != b[i].radius)
            return false;
    return true;
}

// 새 상태가 이만큼 유지돼야 "바뀐 걸로" 친다. 감지 쪽 평활화로도 격자 셀
// 경계 떨림은 안 잡혀서, 안 두면 매초 출구 수만큼 패킷이 나간다
static const int FIRE_POS_HOLD_SEC = 3;                                     

// 실물 전광판이 평면도의 몇 번 전광판인지. 평면도에서 전광판이 여러 개     
// 잡혀도 실물은 하나라, 어느 자리인지 사람이 정해줘야 한다
static const int EVAC_DISPLAY_ID = 3;

// 화재 위치와 대피경로를 전광판으로 보낸다.                                 
// 화재 목록은 한 패킷(0xB2), 경로는 출구마다 한 패킷(0xB1)씩.
// 패킷 사이에 텀을 두는 이유 — 연달아 쏘면 STM 수신 버퍼가 넘쳐 뒤 패킷이 깨짐
static void sendEvacPaths(const std::vector<FireCell>& fires) {
    // 화재 목록 먼저. 경로가 없어도 화재 표시는 갱신해야 한다
    std::vector<uint8_t> firesXYR;
    firesXYR.reserve(fires.size() * 3);
    for (const auto& f : fires) {
        if (firesXYR.size() / 3 >= STM_DISPLAY_EVAC_MAX_FIRES) {
            std::cerr << "[대피경로] 화재가 " << fires.size() << "곳 — 최대 "
                      << STM_DISPLAY_EVAC_MAX_FIRES << "곳까지만 전송\n";
            break;
        }
        firesXYR.push_back((uint8_t)f.x);
        firesXYR.push_back((uint8_t)f.y);
        firesXYR.push_back((uint8_t)f.radius);
    }
    if (!StmDisplay_SendEvacFires(firesXYR.data(), (uint8_t)(firesXYR.size() / 3)))
        std::cerr << "[대피경로] 화재 위치 전송 실패\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    if (!FloorMapStore_HasRoutes()) {
        std::cerr << "[대피경로] 평면도 미등록 — 경로 전송 생략\n";
        return;
    }

    for (const auto& r : FloorMapStore_RoutesForFires(EVAC_DISPLAY_ID, fires)) {   
        // EvacPlanner는 {y,x} 순서, 전광판은 {x,y} 순서 — 뒤집어서 평탄화
        std::vector<uint8_t> xy;
        xy.reserve(r.waypoints.size() * 2);
        for (const Point& p : r.waypoints) {
            xy.push_back((uint8_t)p.x);
            xy.push_back((uint8_t)p.y);
        }
        // routeIndex는 0부터. EvacPlanner가 찾은 출구 순서와 맞아야 한다
        if (!StmDisplay_SendEvacPath((uint8_t)(r.exitId - 1),
                                     xy.data(), (uint8_t)r.waypoints.size()))
            std::cerr << "[대피경로] 출구 " << r.exitId << " 전송 실패 (웨이포인트 "
                      << r.waypoints.size() << "개, 최대 "
                      << STM_DISPLAY_EVAC_MAX_WAYPOINTS << ")\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

// ── 제어 루프 한 바퀴가 들고 다니는 값 ──

namespace {

// tick 사이를 넘어가는 상태 (전광판 재전송 억제용)
struct LoopState {
    int  tick = 0;
    std::vector<FireCell> sentFires;      // 마지막으로 전광판에 보낸 화재들
    std::vector<FireCell> pendingFires;   // 유지 시간을 재는 중인 후보
    long pendingSince = 0;                // 그 후보가 처음 나타난 시각
};

// 이번 tick 에서 여러 단계가 같이 보는 값
struct Tick {
    long          now      = 0;
    SensorReading s{};
    bool          sensorOk = false;
    bool          camFire  = false;
    bool          camSmoke = false;
    int           detCh    = -1;   // 감지된 첫 채널 (스냅샷·클립용)
};

// 마지막 정상 센서값이 이보다 오래되면 Qt에 신뢰 불가로 알린다
const int STALE_SEC = 10;

// 입력 스레드들이 채워둔 값을 한 번에 걷어온다.
// 여기서 블로킹되는 일이 없어야 판단 주기가 밀리지 않는다
Tick beginTick()
{
    Tick t;
    t.now = std::time(nullptr);

    // 센서는 sensorWorker 가 따로 읽는다. 여기서는 마지막 정상값을 복사만 한다.
    // 읽기가 실패해도 값은 그대로 유지되고, 오래됐다는 건 sensorOk 로만 알린다
    // (0으로 떨어뜨리면 "가스 0ppm = 안전"이 되어 위험이 저절로 풀린다)
    {
        std::lock_guard<std::mutex> lk(sensorState.mtx);
        t.s = sensorState.latest;   // 한 번도 못 읽었으면 0 초기값 그대로
    }
    const long okTs = sensorState.lastOkTs.load();
    t.sensorOk = (okTs != 0 && t.now - okTs <= STALE_SEC);

    // 4채널 중 하나라도 감지면 true + 감지 채널 기록 (스냅샷용)
    for (int i = 0; i < 4; i++) {
        if (detState[i].fire)  { t.camFire  = true; if (t.detCh < 0) t.detCh = i; }
        if (detState[i].smoke) { t.camSmoke = true; if (t.detCh < 0) t.detCh = i; }
    }
    // 연기 유지: 감지되면 5초 유지 (1초 추론 vs 매프레임 조회 타이밍 흡수)
    static int smokeHold = 0;
    if (t.camSmoke) smokeHold = 5;
    else if (smokeHold > 0) { --smokeHold; t.camSmoke = true; }

    return t;
}

// 서버 관측값: 감지·대응이 실제로 살아있는가
ServerStatus buildServerStatus(FrameStore& store, const Tick& t)
{
    ServerStatus st{};
    st.sensorOk = t.sensorOk;
    for (int i = 0; i < 4; i++) {
        long f = store.lastFrameTs[i].load();
        long d = detState[i].lastInferTs.load();
        // 프레임이 들어오는데 추론이 멈춘 경우를 잡으려고 둘 다 본다
        st.visionOk[i] = (f && t.now - f <= 3) && (d && t.now - d <= 15);
    }
    ActuatorSnapshot act = Actuator_GetState();
    st.responseOk = responseApplied(QtLink_GetTarget(), act);

    // 해제 체크리스트 중 서버가 판정하는 3항목
    // 죽은 센서의 얼어붙은 값으로 해제되면 안 되므로 sensorOk를 같이 본다
    st.clearSensor   = (judgeState(false, false, t.s).state == "safe") && t.sensorOk;
    st.clearVision   = st.visionOk[0] && st.visionOk[1] && st.visionOk[2] && st.visionOk[3];
    st.clearActuator = st.responseOk;
    return st;
}

// 경고 진입 = 기록 + 스냅샷 (액추에이터 대응은 없음)
void handleWarningEnter(FrameStore& store, const AlarmOutcome& o, const Tick& t)
{
    std::string snap = saveSnapshot(store, t.detCh, "A", t.now);
    ClipRecorder_Start(t.detCh, "A", t.now, o.incidentId);
    g_db.insertEvent(t.now, "A", "warning", o.j.state, o.j.cause,
                     causeToCombo(o.j.cause), "auto", "", "",
                     t.s.gasPpm, t.s.smokePpm, "진행중", 0, snap, o.incidentId, "");
}

// 위험 진입 또는 원인 변경 = 자동 대응 + 전광판 + 기록
void handleDangerEnter(Link& link, FrameStore& store, const AlarmOutcome& o,
                       const Tick& t, LoopState& ls)
{
    std::string src = "자동:" + o.j.cause;
    Response r = decideResponse(o.j.cause);
    bool ok = Actuator_Apply(toActuatorCommand(r), src);
    if (!ok) std::cerr << "[액추에이터] 자동 대응 실패 — " << src << "\n";
    QtLink_SetTarget(r);                       // responseOk 비교 기준 갱신
    QtLink_SendActuator(link, Actuator_GetState());
    StmDisplay_SendAlert(toDisaster(o.j.cause), 1);
    // 대피 화면으로 바뀌는 순간 경로부터 보낸다. 화재 위치는 유지 조건
    // 때문에 몇 초 뒤에나 확정돼서, 그 사이 지도만 있고 경로가 빈다
    sendEvacPaths({});
    ls.sentFires.clear();   // 다음 tick에 화재가 잡히면 정상 흐름으로 갱신됨
    SpeakerAlert_Start();   // 대피 안내 음성 (사이렌은 STM 부저로 별개)

    std::string snap = saveSnapshot(store, t.detCh, "A", t.now);
    ClipRecorder_Start(t.detCh, "A", t.now, o.incidentId);
    g_db.insertEvent(t.now, "A", "danger", o.j.state, o.j.cause,
                     causeToCombo(o.j.cause), "auto", respToText(r), "",
                     t.s.gasPpm, t.s.smokePpm, "진행중", 0, snap, o.incidentId,
                     ok ? "" : "액추에이터 대응 실패");
}

// 위험 해제 = 복귀 대응 + 지속시간 확정
void handleRelease(Link& link, const AlarmOutcome& o, const Tick& t)
{
    // 해제 직후 다시 위험이면 복귀시키지 않는다 (다음 tick에 새 사태로 재대응)
    if (o.wasDanger && o.naturalState != "danger") {
        if (!Actuator_Apply(toActuatorCommand(responseForSafe()), "자동:해제"))
            std::cerr << "[액추에이터] 해제 대응 실패\n";
        QtLink_SetTarget(responseForSafe());
        QtLink_SendActuator(link, Actuator_GetState());
        StmDisplay_SendClear();
        SpeakerAlert_Stop();
    }
    g_db.resolveIncident(o.incidentId, o.durationMs);   // 진행중→해결됨 + 지속시간 일괄
    g_db.insertEvent(t.now, "A", "resolve", "safe", "", "", "auto", "위험 해제", "",
                     t.s.gasPpm, t.s.smokePpm, "해결됨", o.durationMs, "", o.incidentId, "");
}

// 수동 비상 모드 (관리자가 Qt에서 전환 / 대응 재실행)
// 둘 다 "현재 원인에 맞는 대응 실행"으로 동작이 같다.
// 이미 위험이면 상태 변화 없이 대응만 다시 나간다
void handleManualEmergency(Link& link, FrameStore& store, const AlarmOutcome& o,
                           const Tick& t, LoopState& ls)
{
    Response r = decideResponse(o.j.cause);
    bool ok = Actuator_Apply(toActuatorCommand(r), "비상:" + o.j.cause);
    if (!ok) std::cerr << "[액추에이터] 비상 대응 실패\n";
    QtLink_SetTarget(r);
    QtLink_SendActuator(link, Actuator_GetState());
    StmDisplay_SendAlert(toDisaster(o.j.cause), 1);
    sendEvacPaths({});   // 자동 전환과 같은 이유 — 경로부터 먼저
    ls.sentFires.clear();
    SpeakerAlert_Start();

    std::string snap = saveSnapshot(store, t.detCh, "A", t.now);
    ClipRecorder_Start(t.detCh, "A", t.now, o.incidentId);
    // 재실행은 상태가 안 바뀌는 조치라 category를 나눈다 (전환 횟수 집계에 섞이면 안 됨)
    g_db.insertEvent(t.now, "A", o.emergReapply ? "emergency_reapply" : "emergency",
                     o.j.state, o.j.cause,
                     causeToCombo(o.j.cause), "manual", respToText(r), o.admin,
                     t.s.gasPpm, t.s.smokePpm, "진행중", 0, snap, o.incidentId,
                     ok ? "" : "액추에이터 대응 실패");
}

// 전광판 대피경로: 화재 위치가 바뀌었을 때만 보낸다
// 매초 보내면 출구 수만큼 패킷 + 대기가 반복되고 화면도 깜빡인다
void updateEvacPaths(LoopState& ls, const Tick& t)
{
    std::vector<FireCell> nowFires = collectFires();
    if (sameFires(nowFires, ls.sentFires)) return;

    if (!sameFires(nowFires, ls.pendingFires)) {
        ls.pendingFires = nowFires;    // 새 후보 등장 → 유지 시간 다시 잼
        ls.pendingSince = t.now;
    } else if (t.now - ls.pendingSince >= FIRE_POS_HOLD_SEC) {
        sendEvacPaths(nowFires);
        ls.sentFires = nowFires;
        std::cout << "[대피경로] 화재 " << ls.sentFires.size() << "곳 갱신\n";
    }
}

// 전광판 ACK(0xB0)로 통신 상태 확인. 매초 찍으면 시끄러우니 바뀌는 순간만
// (SendUpdate 안에서 매초 갱신되므로 별도 폴링 불필요)
void reportDisplayLink()
{
    static bool prevOk = true;
    bool ok = StmDisplay_GetLinkOk();
    if (ok != prevOk) {
        std::cerr << "[전광판] 통신 " << (ok ? "복구됨" : "불량 (ACK 없음)") << "\n";
        prevOk = ok;
    }
}

// 액추에이터 상태 주기 보고 + Qt 접속 직후 즉시 1회
// 주기만 있으면 접속~다음 tick 사이(최대 5초)에 Qt가 값을 못 받는다.
// 그 사이 끊기면 "확인 중"에서 영영 안 벗어남
void serveQtConnection(Link& link, FrameStore& store, LoopState& ls)
{
    static bool prevConnected = false;
    bool nowConnected  = link.connected();
    bool justConnected = nowConnected && !prevConnected;
    prevConnected = nowConnected;

    if (justConnected) {
        QtLink_PushIgnoreRegions(link);   // 저장된 ROI 복원 (Q3 회신)

        // 크기만 담은 빈 감지 메시지. Qt는 ROI 좌표를 원본 프레임 기준으로
        // 변환하는데 그 크기를 detection에서만 알 수 있다. 감지가 한 번도
        // 없던 채널은 detection이 안 나가서 영영 모르는 상태가 된다
        for (int ch = 0; ch < 4; ch++) {
            int w = store.frameW[ch].load(), h = store.frameH[ch].load();
            if (w > 0 && h > 0) QtLink_SendDetection(link, ch, 0, w, h, false, {});
        }
    }

    if (justConnected || ++ls.tick % 5 == 0) {
        Actuator_Poll();   // STM에 상태 요청(0x40) → linkOk 갱신. 안 하면 끊겨도 모름
        QtLink_SendActuator(link, Actuator_GetState());
    }
}

}   // namespace

// ── 제어 루프: 1초마다 판단 → 대응 → 기록 → 전송 ──
// 센서·영상 입력은 각자 스레드가 하고, 여기서는 그 결과만 받아 쓴다.
// 판단과 대응을 나누지 않은 이유 — 위험 판정과 밸브 차단 사이에 큐가 끼면
// 지연되거나 순서가 뒤집힌다
void controlLoop(Link& link, FrameStore& store, AlarmState& alarm)
{
    LoopState ls;

    while (true) {
        Tick t = beginTick();

        AlarmOutcome o  = alarm.update(judgeState(t.camFire, t.camSmoke, t.s), t.now);
        ServerStatus st = buildServerStatus(store, t);

        if (o.warnEntered)                        handleWarningEnter(store, o, t);
        if (o.dangerEntered && !o.emergEntered)   handleDangerEnter(link, store, o, t, ls);
        if (o.released)                           handleRelease(link, o, t);
        if (o.emergEntered)                       handleManualEmergency(link, store, o, t, ls);

        QtLink_SendSensor(link, t.s, o, st);
        StmDisplay_SendUpdate(toDisplayUpdate(t.s));

        updateEvacPaths(ls, t);
        reportDisplayLink();

        if (t.sensorOk) {   // 고장 중 0값을 이력에 남기면 통계가 망가진다
            g_db.insertSensor(t.now, "A", t.s.temp, t.s.humidity,
                              t.s.gasPpm, t.s.smokePpm, t.s.flameVal, o.j.state);
        }

        serveQtConnection(link, store, ls);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
