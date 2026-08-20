#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <atomic>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <fstream> 
#include <sys/stat.h>

#include "opencv/FireDetectionRuntime.h"
#include "opencv/SmokeDetectionRuntime.h"
#include "opencv/PersonMetadataReceiver.h"
#include "opencv/AppConfig.h"

#include "net/link.h"
#include "sensors/sensor_reader.h"
#include "actuator/actuator_control.h"
#include "display/stm_display.h"
#include "../drivers/stm_uart_display/stm_display_protocol.h" 
#include "judgement.h"
#include "alarm_state.h"
#include "qt_link.h"
#include "roi/roi_store.h"
#include "floormap/floormap_store.h"             
#include "db/Database.h"
#include "audio/speaker_alert.h"   
#include "clip/clip_recorder.h"    
#include "calib/calib_store.h"    

// 4채널 프레임 공유 저장소. 채널별 mutex로 워커/센서 스레드 간 경합 방지
struct FrameStore {
    cv::Mat frames[4];
    std::mutex mtx[4];
    std::atomic<long> lastFrameTs[4]{};   // 마지막 프레임 수신 시각 (visionOk 판정용)
    std::atomic<int> frameW[4]{};   // 원본 프레임 크기. Qt가 ROI 좌표 변환에 필요   
    std::atomic<int> frameH[4]{};
};

// 화재 하나의 평면도 좌표. 전광판 표시·경로 필터링용                      
struct FireCell {
    int x = 0, y = 0, radius = 0;
};

// 채널별 최신 감지 상태. 워커가 갱신, 판단(센서 스레드)이 읽음
struct DetectionState {
    std::atomic<bool> fire{false};
    std::atomic<bool> smoke{false};
    std::atomic<long> lastInferTs{0};     // 마지막 추론 결과 시각 (visionOk 판정용) 

    // 이 채널에서 잡힌 화재들. 떨어진 불은 각각 경로를 막으므로 전부 보관한다
    // (개수가 변해서 atomic으로 못 다룸 — 잠금으로 처리)
    std::mutex fireMtx;
    std::vector<FireCell> fires;
};                                                                    

DetectionState detState[4];

Database g_db;   // 전역 DB. main에서 open

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
static const int EVAC_DISPLAY_ID = 1;

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

    for (const auto& r : FloorMapStore_RoutesFor(EVAC_DISPLAY_ID)) {
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
            

// ── 센서 스레드: 1초마다 읽기 → 판단 → 대응 → 기록 → 전송 ──
void sensorWorker(Link& link, FrameStore& store, AlarmState& alarm) {
    int tick = 0;
    SensorReading lastGood{};      // 마지막으로 정상 읽은 값               
    long lastGoodTs = 0;           // 그 값을 읽은 시각 (0 = 아직 없음)
    const int STALE_SEC = 10;      // 이보다 오래되면 Qt에 신뢰 불가로 알림

    // 전광판 대피경로 전송 상태                                              
    std::vector<FireCell> sentFires;      // 마지막으로 전광판에 보낸 화재들
    std::vector<FireCell> pendingFires;   // 유지 시간을 재는 중인 후보
    long pendingSince = 0;                // 그 후보가 처음 나타난 시각
    bool hasSent = false;                 // 한 번이라도 보냈나 (최초 전송 판단용) 

    while (true) {
        long now = std::time(nullptr);   // 아래에 있던 선언을 여기로 올림   
        SensorReading s;
        bool sensorOk = true; 
        static bool prevSensorOk = true;   // 센서 상태 로그: 변화 시에만 (실패 시 1초마다 도배 방지) 
        if (SensorReader_Read(s)) {                                      
            if (!prevSensorOk) std::cout << "[센서] 복구됨\n";
            prevSensorOk = true;
            lastGood = s; lastGoodTs = now;
        } else {
            if (prevSensorOk) std::cerr << "[센서] 읽기 실패\n";
            prevSensorOk = false;
            // 0으로 채우면 "가스 0ppm = 안전"이 되어 위험이 저절로 풀린다. 
            // 값은 마지막 정상값을 유지하고, 오래됐다는 건 sensorOk로만 알린다
            s = lastGood;   // 한 번도 못 읽었으면 0 초기값 그대로
            sensorOk = (lastGoodTs != 0 && now - lastGoodTs <= STALE_SEC);  
        }                                                                

        // 4채널 중 하나라도 감지면 true + 감지 채널 기록 (스냅샷용)
        bool camFire = false, camSmoke = false;
        int  detCh = -1;
        for (int i = 0; i < 4; i++) {
            if (detState[i].fire)  { camFire  = true; if (detCh < 0) detCh = i; }
            if (detState[i].smoke) { camSmoke = true; if (detCh < 0) detCh = i; }
        }
        // 연기 유지: 감지되면 5초 유지 (1초 추론 vs 매프레임 조회 타이밍 흡수)
        static int smokeHold = 0;
        if (camSmoke) smokeHold = 5;
        else if (smokeHold > 0) { --smokeHold; camSmoke = true; }

        AlarmOutcome o = alarm.update(judgeState(camFire, camSmoke, s), now);

        // ── 서버 관측값: 감지·대응이 실제로 살아있는가 ──                
        ServerStatus st{};
        st.sensorOk = sensorOk;
        for (int i = 0; i < 4; i++) {
            long f = store.lastFrameTs[i].load();
            long d = detState[i].lastInferTs.load();
            // 프레임이 들어오는데 추론이 멈춘 경우를 잡으려고 둘 다 본다
            st.visionOk[i] = (f && now - f <= 3) && (d && now - d <= 15);
        }
        ActuatorSnapshot act = Actuator_GetState();
        st.responseOk = responseApplied(QtLink_GetTarget(), act);

        // 해제 체크리스트 중 서버가 판정하는 3항목
        // 죽은 센서의 얼어붙은 값으로 해제되면 안 되므로 sensorOk를 같이 본다
        st.clearSensor   = (judgeState(false, false, s).state == "safe") && sensorOk;
        st.clearVision   = st.visionOk[0] && st.visionOk[1] && st.visionOk[2] && st.visionOk[3];
        st.clearActuator = st.responseOk;                                

        // 경고 진입 = 기록 + 스냅샷 (액추에이터 대응은 없음)
        if (o.warnEntered) {
            std::string snap = saveSnapshot(store, detCh, "A", now);
            ClipRecorder_Start(detCh, "A", now, o.incidentId);         
            g_db.insertEvent(now, "A", "warning", o.j.state, o.j.cause,
                             causeToCombo(o.j.cause), "auto", "", "",
                             s.gasPpm, s.smokePpm, "진행중", 0, snap, o.incidentId, "");
        }

        // 위험 진입 또는 원인 변경 = 자동 대응 + 전광판 + 기록
        // 수동 전환으로 위험이 된 경우는 아래 비상 블록이 처리하므로 건너뛴다 
        if (o.dangerEntered && !o.emergEntered) { 
            std::string src = "자동:" + o.j.cause;
            Response r = decideResponse(o.j.cause);
            bool ok = Actuator_Apply(r, src);                            
            if (!ok) std::cerr << "[액추에이터] 자동 대응 실패 — " << src << "\n";
            QtLink_SetTarget(r);                       // responseOk 비교 기준 갱신
            QtLink_SendActuator(link, Actuator_GetState());
            StmDisplay_SendAlert(o.j.cause, 1);
            // 대피 화면으로 바뀌는 순간 경로부터 보낸다. 화재 위치는 유지 조건    
            // 때문에 몇 초 뒤에나 확정돼서, 그 사이 지도만 있고 경로가 빈다
            sendEvacPaths({});
            sentFires.clear();   // 다음 tick에 화재가 잡히면 정상 흐름으로 갱신됨   
            SpeakerAlert_Start();   // 대피 안내 음성 (사이렌은 STM 부저로 별개)    

            std::string snap = saveSnapshot(store, detCh, "A", now);
            ClipRecorder_Start(detCh, "A", now, o.incidentId);
            g_db.insertEvent(now, "A", "danger", o.j.state, o.j.cause,
                             causeToCombo(o.j.cause), "auto", respToText(r), "",
                             s.gasPpm, s.smokePpm, "진행중", 0, snap, o.incidentId,
                             ok ? "" : "액추에이터 대응 실패");         
        }

        // 위험 해제 = 복귀 대응 + 지속시간 확정
        if (o.released) {
            // 해제 직후 다시 위험이면 복귀시키지 않는다 (다음 tick에 새 사태로 재대응) 
            if (o.wasDanger && o.naturalState != "danger") {
                if (!Actuator_Apply(responseForSafe(), "자동:해제"))        
                    std::cerr << "[액추에이터] 해제 대응 실패\n"; 
                QtLink_SetTarget(responseForSafe());
                QtLink_SendActuator(link, Actuator_GetState());
                StmDisplay_SendClear();
                SpeakerAlert_Stop(); 
            }                                                              
            const char* resp = o.wasDanger ? "위험 해제" : "경고 해제";     

            g_db.resolveIncident(o.incidentId, o.durationMs);   // 진행중→해결됨 + 지속시간 일괄
            g_db.insertEvent(now, "A", "resolve", "safe", "", "", "auto", "위험 해제", "",
                             s.gasPpm, s.smokePpm, "해결됨", o.durationMs, "", o.incidentId, "");
        }

        // ── 수동 비상 모드 (관리자가 Qt에서 전환 / 대응 재실행) ──       
        // 둘 다 "현재 원인에 맞는 대응 실행"으로 동작이 같다.
        // 이미 위험이면 상태 변화 없이 대응만 다시 나간다
        if (o.emergEntered) {
            Response r = decideResponse(o.j.cause);
            bool ok = Actuator_Apply(r, "비상:" + o.j.cause);   // 조합은 서버가 정하므로 auto로
            if (!ok) std::cerr << "[액추에이터] 비상 대응 실패\n";
            QtLink_SetTarget(r);   
            QtLink_SendActuator(link, Actuator_GetState());
            StmDisplay_SendAlert(o.j.cause, 1);
            sendEvacPaths({});   // 자동 전환과 같은 이유 — 경로부터 먼저         
            sentFires.clear();    
            SpeakerAlert_Start();

            std::string snap = saveSnapshot(store, detCh, "A", now);
            ClipRecorder_Start(detCh, "A", now, o.incidentId); 
            // 재실행은 상태가 안 바뀌는 조치라 category를 나눈다 (전환 횟수 집계에 섞이면 안 됨) 
            g_db.insertEvent(now, "A", o.emergReapply ? "emergency_reapply" : "emergency", 
                             o.j.state, o.j.cause,
                             causeToCombo(o.j.cause), "manual", respToText(r), o.admin,
                             s.gasPpm, s.smokePpm, "진행중", 0, snap, o.incidentId,
                             ok ? "" : "액추에이터 대응 실패");
        }                                                             

        QtLink_SendSensor(link, s, o,st);
        StmDisplay_SendUpdate(s, o.j.state);

        // ── 전광판 대피경로: 화재 위치가 바뀌었을 때만 보낸다 ──           
        // 매초 보내면 출구 수만큼 패킷 + 대기가 반복되고 화면도 깜빡인다
        std::vector<FireCell> nowFires = collectFires();
        if (!sameFires(nowFires, sentFires)) {
            if (!sameFires(nowFires, pendingFires)) {
                pendingFires = nowFires;    // 새 후보 등장 → 유지 시간 다시 잼
                pendingSince = now;
            } else if (now - pendingSince >= FIRE_POS_HOLD_SEC) {
                sendEvacPaths(nowFires);                                    

                sentFires = nowFires;
                hasSent = true;
                std::cout << "[대피경로] 화재 " << sentFires.size() << "곳 갱신\n";
            }
        }                                                              

        // 전광판 ACK(0xB0)로 통신 상태 확인. 매초 찍으면 시끄러우니 바뀌는 순간만 
        // (SendUpdate 안에서 매초 갱신되므로 별도 폴링 불필요)
        static bool prevDisplayOk = true;
        bool displayOk = StmDisplay_GetLinkOk();
        if (displayOk != prevDisplayOk) {
            std::cerr << "[전광판] 통신 " << (displayOk ? "복구됨" : "불량 (ACK 없음)") << "\n";
            prevDisplayOk = displayOk;
        }                                                                
        if (sensorOk) {   // 고장 중 0값을 이력에 남기면 통계가 망가진다     
            g_db.insertSensor(now, "A", s.temp, s.humidity, s.gasPpm, s.smokePpm, s.flameVal, o.j.state);
        }    

        // 액추에이터 상태 주기 보고 + Qt 접속 직후 즉시 1회                 
        // 주기만 있으면 접속~다음 tick 사이(최대 5초)에 Qt가 값을 못 받는다.
        // 그 사이 끊기면 "확인 중"에서 영영 안 벗어남
        static bool prevConnected = false;
        bool nowConnected = link.connected();
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

        if (justConnected || ++tick % 5 == 0) {
            Actuator_Poll();   // STM에 상태 요청(0x40) → linkOk 갱신. 안 하면 끊겨도 모름
            QtLink_SendActuator(link, Actuator_GetState());
        }                                                                                                            

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}


static bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// 채널별 ArUco 보정 파일 3종을 읽어 좌표 변환을 켠다.
// 하나라도 없으면 좌표만 끄고 화재·연기 감지는 그대로 돈다 (gridValid=false로 전송)
static void loadFactoryMapping(FireDetectionRuntime& runtime, int ch) {
    const std::size_t idx = (std::size_t)ch;

    if (!runtime.loadArucoBoardConfiguration(FIRE_ARUCO_CONFIG_PATH, idx)) {
        std::cout << "[좌표] cam" << ch + 1 << " 미설정 — 감지는 계속, 좌표만 비활성 ("
                  << runtime.arucoMappingError() << ")\n";
        CalibStore_SetStage(ch, CalibStage::NoBoard, runtime.arucoMappingError());  
        return;
    }

    const std::string calib = std::string(FIRE_CAMERA_CALIBRATION_DIR)
                            + "/camera_calibration_ch" + std::to_string(ch + 1) + ".yml";
    if (!fileExists(calib) || !runtime.loadCameraCalibration(calib, idx)) {
        std::cerr << "[좌표] cam" << ch + 1 << " 렌즈 보정값 없음 — 좌표 비활성 ("
                  << runtime.cameraCalibrationError() << ")\n";
        CalibStore_SetStage(ch, CalibStage::NoLens, runtime.cameraCalibrationError());
        return;
    }

    const std::string homo = std::string(FIRE_STATIC_HOMOGRAPHY_DIR)
                           + "/homography_ch" + std::to_string(ch + 1) + ".yml";
    if (!fileExists(homo) || !runtime.loadStaticHomography(homo, idx)) {
        std::cerr << "[좌표] cam" << ch + 1 << " 보정 미완료 — 좌표 비활성 ("
                  << runtime.arucoMappingError() << ")\n";
        CalibStore_SetStage(ch, CalibStage::NoHomography, runtime.arucoMappingError());  
        return;
    }

    std::cout << "[좌표] cam" << ch + 1 << " 보정 적용 완료\n";
    CalibStore_SetStage(ch, CalibStage::Ready, "");   
}                                                                           


// ── 채널 워커: RTSP 연결 → 프레임 읽기 → 감지 → 전송. 끊기면 재연결 ──
void worker(int ch, FrameStore& store, Link& link, SmokeDetectionRuntime& smoke) {
    std::string url = "rtsp://localhost:8554/cam" + std::to_string(ch + 1);

    PersonMetadataReceiver person;
    person.start(url);              // 카메라 WiseAI 사람 메타데이터 수신 (FFmpeg)

    FireDetectionRuntime runtime;   // 복사 금지 타입 → 채널당 지역변수 1개
    loadFactoryMapping(runtime, ch);   // ArUco 보정 로드. 없으면 좌표만 비활성
    int roiVer = -1;  // ROI 설정 버전. 바뀔 때만 런타임에 반영
    int calibVer = CalibStore_ReloadVersion(ch);   // Qt 재로드 요청 감시용   
    std::uint64_t frameId = 0;
    bool wasShowingBoxes = false;
    bool prevSmoke = false, prevPerson = false, prevFire = false;   // 로그: 변화 시에만 출력용

    while (true) {
        cv::VideoCapture cap;
        cap.open(url, cv::CAP_FFMPEG);
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);   // 버퍼 1프레임 = 항상 최신 처리

        if (!cap.isOpened()) {
            std::cerr << "[cam" << ch + 1 << "] 연결 실패, 3초 후 재시도\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        std::cout << "[cam" << ch + 1 << "] 연결 성공\n";
        runtime.resetStream();   // 재연결이면 이전 추적 상태 폐기
        roiVer = -1;             // 재연결 후 ROI도 다시 넣는다 (상태 유실 대비)

        cv::Mat frame;
        while (true) {
            if (!cap.read(frame) || frame.empty()) {
                std::cerr << "[cam" << ch + 1 << "] 프레임 읽기 실패, 재연결\n";
                break;
            }
            {
                std::lock_guard<std::mutex> lock(store.mtx[ch]);
                store.frames[ch] = frame.clone();
            }
            store.lastFrameTs[ch] = std::time(nullptr);   // 감지 생존 확인용
            store.frameW[ch] = frame.cols;                                         
            store.frameH[ch] = frame.rows;

            // 이벤트는 예고 없이 터지므로 평소에도 직전 3초를 담아둬야 한다 
            ClipRecorder_Push(ch, frame);         

            // 보정 재로드 요청 확인. 평소엔 정수 하나만 읽고 지나간다           
            // 보정을 다시 잡을 때마다 서버를 재시작하면 4채널 감지가 전부 끊긴다
            int cver = CalibStore_ReloadVersion(ch);
            if (cver != calibVer) {
                calibVer = cver;
                std::cout << "[좌표] cam" << ch + 1 << " 보정 재로드 요청 수신\n";
                loadFactoryMapping(runtime, ch);   // 결과는 CalibStore에 기록됨
            }                                      

            // ROI 갱신 확인. 평소엔 정수 하나만 읽고 지나간다               
            // 화재·연기 엔진이 각각 설정을 받으므로 둘 다 넣어야 한다
            int ver = RoiStore_Version(ch);
            if (ver != roiVer) {
                roiVer = ver;
                IgnoreRegionConfig fireCfg, smokeCfg;
                RoiStore_Get(ch, fireCfg, smokeCfg);
                runtime.setIgnoreRegionConfig(fireCfg);
                smoke.setIgnoreRegionConfig(ch, smokeCfg);
                std::cout << "[ROI] cam" << ch + 1 << " 적용 — 화재 "
                          << fireCfg.regions.size() << "개, 연기 "
                          << smokeCfg.regions.size() << "개\n";
            }                                                          

            runtime.submitFrame(frame, frameId);
            smoke.submitFrame(ch, frame, frameId);
            frameId++;

            FireRuntimeSnapshot snap = runtime.poll();

            // 마커 개수·좌표 오차는 매 프레임 바뀐다. Qt가 물어볼 때 최신값을 주려고 계속 담아둔다 
            CalibStore_SetLive(ch, snap.arucoMapping);                                          

            // ── 연기 (NCNN) ──
            SmokeRuntimeSnapshot ssnap = smoke.poll(ch);
            if (ssnap.hasResult)                                // 결과 있을 때만 갱신
                detState[ch].smoke = ssnap.smokeDetected;       // 없으면 이전 값 유지 → 깜빡임 방지

            bool nowSmoke = ssnap.hasResult && ssnap.smokeDetected;
            if (nowSmoke && !prevSmoke)
                std::cout << "[cam" << ch+1 << "] 연기 감지 (score " << ssnap.smokeScore << ")\n";
            else if (!nowSmoke && prevSmoke)
                std::cout << "[cam" << ch+1 << "] 연기 해제\n";
            prevSmoke = nowSmoke;

            // 추론 파이프라인 생존 확인용. 검출이 0건이어도 "결과는 나온 것"이므로  
            // boxIsFresh(알람 중일 때만 참)가 아니라 resultIsFresh를 본다
            if (snap.resultIsFresh || ssnap.resultIsFresh)
                detState[ch].lastInferTs = std::time(nullptr);                  

            // ── 사람 (카메라 WiseAI) ──
            PersonMetadataFrame pf = person.snapshot(frame.size());
            bool nowPerson = !pf.persons.empty();
            if (nowPerson && !prevPerson)
                std::cout << "[cam" << ch+1 << "] 사람 감지 (" << pf.persons.size() << "명)\n";
            else if (!nowPerson && prevPerson)
                std::cout << "[cam" << ch+1 << "] 사람 사라짐\n";
            prevPerson = nowPerson;

            // 사람 좌표 전송 — 매 프레임은 과함. 0.5초 간격(30fps 기준)
            if (frameId % 15 == 0) {
                std::vector<PersonBox> persons;
                for (const auto& p : pf.persons)
                    persons.push_back({ p.box.x, p.box.y, p.box.width, p.box.height,
                                        (float)p.confidence });
                QtLink_SendPerson(link, ch, frame.cols, frame.rows, persons);
            }

            // ── 화재 + 연기 박스 전송 ──                                 
            if (snap.boxIsFresh || ssnap.boxIsFresh) {
                std::vector<DetBox> boxes;
                if (snap.boxIsFresh) {
                    bool hasFire = false;
                    std::vector<FireCell> fires;   // 이 프레임의 화재 좌표들    
                    for (const auto& b : snap.detection.boxes) {
                        if (b.type == DetectionType::FIRE) hasFire = true;
                        // 떨어진 불은 각각 경로를 막으므로 전부 모은다
                        if (b.type == DetectionType::FIRE && b.gridPositionValid)
                            fires.push_back({ b.gridX, b.gridY, b.displayRadiusCells });
                                                                                                                            
                        boxes.push_back({ b.box.x, b.box.y, b.box.width, b.box.height,
                                          b.type == DetectionType::FIRE ? "FIRE" : "SMOKE",
                                          (float)b.score });
                    }
                    {   // 감지 스레드가 쓰고 센서 스레드가 읽는다                 // <- 처음
                        std::lock_guard<std::mutex> lk(detState[ch].fireMtx);
                        detState[ch].fires = std::move(fires);
                    }                                                             // <- 끝
                    // 화재 상태는 화재 결과가 왔을 때만 갱신. 연기 결과에 같이 지우면
                    // 감지 중인 화재가 연기 추론 주기(1초)마다 꺼진다
                    detState[ch].fire = hasFire && snap.alarm.alarmActive;

                    bool nowFire = detState[ch].fire;   // 화재 로그: 확정/해제 순간만
                    if (nowFire && !prevFire)
                        std::cout << "[cam" << ch+1 << "] 화재 감지 (알람 확정)\n";
                    else if (!nowFire && prevFire)
                        std::cout << "[cam" << ch+1 << "] 화재 해제\n";
                    prevFire = nowFire;
                }
                if (ssnap.boxIsFresh) {
                    for (const auto& b : ssnap.detection.boxes)
                        boxes.push_back({ b.box.x, b.box.y, b.box.width, b.box.height,
                                          "SMOKE", (float)b.score });
                }

                QtLink_SendDetection(link, ch, (int)snap.resultFrameId,
                                     frame.cols, frame.rows,
                                     snap.alarm.alarmActive, boxes);
                wasShowingBoxes = true;
            }                                                             
            else if (wasShowingBoxes && !snap.alarm.alarmActive) {
                QtLink_SendDetection(link, ch, 0, frame.cols, frame.rows, false, {});
                wasShowingBoxes = false;
                detState[ch].fire = false;
                {   // 불이 꺼지면 좌표도 비운다. 안 그러면 마지막 위치가 계속 남음 
                    std::lock_guard<std::mutex> lk(detState[ch].fireMtx);
                    detState[ch].fires.clear();
                }   
            }
        }

        cap.release();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

int main() {
    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS",
           "rtsp_transport;tcp|fflags;nobuffer|flags;low_delay", 1);   // 저지연 옵션
    cv::setNumThreads(1);   // OpenCV 채널당 1스레드 = 멀티채널 최적화 핵심

    // ── 초기화. 하나 실패해도 나머지는 계속 감 ──
    Link* link = CreateLink();
    if (!link->start(9999)) {
        std::cerr << "[링크] 시작 실패\n";
        return 1;
    }
    if (!g_db.open(DB_PATH))
        std::cerr << "[DB] 초기화 실패 — DB 없이 계속 진행\n";
    // 녹화가 끝나는 건 13초 뒤 저장 스레드다. 그때 DB 행에 경로만 채워 넣는다   
    ClipRecorder_Init([](long incidentId, long ts, const std::string& path) {
        g_db.updateClipPath(incidentId, ts, path);
    });                                                                                                                   
    RoiStore_Load();   // 저장된 ROI 복원. 파일 없으면 빈 상태로 시작  
    FloorMapStore_Load();   // 저장된 평면도 변환 결과 복원 
    if (!Actuator_Init("/dev/stm_actuator"))          // STM 액추에이터 보드 (USB) (심볼릭링크)
        std::cerr << "[액추에이터] 초기화 실패 — 계속 진행\n";
    Actuator_Apply(responseForSafe(), "자동:초기화");   // 재시작 후 상태를 알 수 없으므로 평상으로 맞춤 
    QtLink_SetTarget(responseForSafe());                                              
    if (!StmDisplay_Open("/dev/stm_display"))        // STM 전광판 보드 (GPIO UART) (심볼릭링크)
        std::cerr << "[전광판] 초기화 실패 — 계속 진행\n";    
    StmDisplay_SendClear();   // 이전 실행이 대피 화면에서 끝났을 수 있으므로 평상 복귀                
    SpeakerAlert_Stop();      // 이전 실행이 재생 중 종료됐을 수 있으므로 정리   

    AlarmState alarm;
    alarm.setIncidentSeqStart(g_db.maxIncidentId());   // 재시작해도 번호가 안 겹치게
    FrameStore store;

    SmokeDetectionRuntime smoke(4,
        smoke_config::MODEL_PARAM_PATH, smoke_config::MODEL_BIN_PATH);
    if (smoke.isModelReady()) std::cout << "[연기] 모델 로드 완료\n";
    else std::cerr << "[연기] 모델 로드 실패: " << smoke.modelError() << "\n";

    // ── 스레드 기동. 스레드는 여기서만 만든다 ──
    std::thread sensorThread(sensorWorker, std::ref(*link), std::ref(store), std::ref(alarm));
    sensorThread.detach();

    std::thread recvThread(QtLink_RecvWorker, std::ref(*link), std::ref(alarm), std::ref(g_db));
    recvThread.detach();

    std::thread cams[4];
    for (int i = 0; i < 4; i++)
        cams[i] = std::thread(worker, i, std::ref(store), std::ref(*link), std::ref(smoke));
    for (int i = 0; i < 4; i++)
        cams[i].join();
    
    ClipRecorder_Shutdown();   // 녹화 중이던 것도 모인 데까지 저장하고 스레드 정리  
    link->stop();
    delete link;
    return 0;
}