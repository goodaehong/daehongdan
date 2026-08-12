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
#include <sys/stat.h>

#include "opencv/FireDetectionRuntime.h"
#include "opencv/SmokeDetectionRuntime.h"
#include "opencv/PersonMetadataReceiver.h"
#include "opencv/AppConfig.h"

#include "net/link.h"
#include "sensors/sensor_reader.h"
#include "actuator/actuator_control.h"
#include "display/stm_display.h"
#include "judgement.h"
#include "alarm_state.h"
#include "qt_link.h"
#include "db/Database.h"
#include "audio/speaker_alert.h"   

// 4채널 프레임 공유 저장소. 채널별 mutex로 워커/센서 스레드 간 경합 방지
struct FrameStore {
    cv::Mat frames[4];
    std::mutex mtx[4];
    std::atomic<long> lastFrameTs[4]{};   // 마지막 프레임 수신 시각 (visionOk 판정용)
};

// 채널별 최신 감지 상태. 워커가 갱신, 판단(센서 스레드)이 읽음
struct DetectionState {
    std::atomic<bool> fire{false};
    std::atomic<bool> smoke{false};
    std::atomic<long> lastInferTs{0};     // 마지막 추론 결과 시각 (visionOk 판정용) 
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

// ── 센서 스레드: 1초마다 읽기 → 판단 → 대응 → 기록 → 전송 ──
void sensorWorker(Link& link, FrameStore& store, AlarmState& alarm) {
    int tick = 0;
    SensorReading lastGood{};      // 마지막으로 정상 읽은 값               
    long lastGoodTs = 0;           // 그 값을 읽은 시각 (0 = 아직 없음)
    const int STALE_SEC = 10;      // 이보다 오래되면 Qt에 신뢰 불가로 알림

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
            g_db.insertEvent(now, "A", "warning", o.j.state, o.j.cause,
                             causeToCombo(o.j.cause), "auto", "", "",
                             s.gasPpm, s.smokePpm, "진행중", 0, snap, o.incidentId, "");
        }

        // 위험 진입 또는 원인 변경 = 자동 대응 + 전광판 + 기록
        // 수동 전환으로 위험이 된 경우는 아래 비상 블록이 처리하므로 건너뛴다  // <- 처음
        if (o.dangerEntered && !o.emergEntered) { 
            std::string src = "자동:" + o.j.cause;
            Response r = decideResponse(o.j.cause);
            bool ok = Actuator_Apply(r, src);                            
            if (!ok) std::cerr << "[액추에이터] 자동 대응 실패 — " << src << "\n";
            QtLink_SetTarget(r);                       // responseOk 비교 기준 갱신
            QtLink_SendActuator(link, Actuator_GetState());
            StmDisplay_SendAlert(o.j.cause, 1);
             SpeakerAlert_Start();   // 대피 안내 음성 (사이렌은 STM 부저로 별개)    

            std::string snap = saveSnapshot(store, detCh, "A", now);
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

        // ── 수동 비상 모드 (관리자가 Qt에서 전환 / 대응 재실행) ──        // <- 처음
        // 둘 다 "현재 원인에 맞는 대응 실행"으로 동작이 같다.
        // 이미 위험이면 상태 변화 없이 대응만 다시 나간다
        if (o.emergEntered) {
            Response r = decideResponse(o.j.cause);
            bool ok = Actuator_Apply(r, "비상:" + o.j.cause);   // 조합은 서버가 정하므로 auto로
            if (!ok) std::cerr << "[액추에이터] 비상 대응 실패\n";
            QtLink_SetTarget(r);   
            QtLink_SendActuator(link, Actuator_GetState());
            StmDisplay_SendAlert(o.j.cause, 1);
            SpeakerAlert_Start();

            std::string snap = saveSnapshot(store, detCh, "A", now);
            // 재실행은 상태가 안 바뀌는 조치라 category를 나눈다 (전환 횟수 집계에 섞이면 안 됨) 
            g_db.insertEvent(now, "A", o.emergReapply ? "emergency_reapply" : "emergency", 
                             o.j.state, o.j.cause,
                             causeToCombo(o.j.cause), "manual", respToText(r), o.admin,
                             s.gasPpm, s.smokePpm, "진행중", 0, snap, o.incidentId,
                             ok ? "" : "액추에이터 대응 실패");
        }                                                             

        QtLink_SendSensor(link, s, o,st);
        StmDisplay_SendUpdate(s, o.j.state);
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

        if (justConnected || ++tick % 5 == 0) {
            Actuator_Poll();   // STM에 상태 요청(0x40) → linkOk 갱신. 안 하면 끊겨도 모름
            QtLink_SendActuator(link, Actuator_GetState());
        }                                                                                                            

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ── 채널 워커: RTSP 연결 → 프레임 읽기 → 감지 → 전송. 끊기면 재연결 ──
void worker(int ch, FrameStore& store, Link& link, SmokeDetectionRuntime& smoke) {
    std::string url = "rtsp://localhost:8554/cam" + std::to_string(ch + 1);

    PersonMetadataReceiver person;
    person.start(url);              // 카메라 WiseAI 사람 메타데이터 수신 (FFmpeg)

    FireDetectionRuntime runtime;   // 복사 금지 타입 → 채널당 지역변수 1개
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

            runtime.submitFrame(frame, frameId);
            smoke.submitFrame(ch, frame, frameId);
            frameId++;

            FireRuntimeSnapshot snap = runtime.poll();

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
                    for (const auto& b : snap.detection.boxes) {
                        if (b.type == DetectionType::FIRE) hasFire = true;
                        boxes.push_back({ b.box.x, b.box.y, b.box.width, b.box.height,
                                          b.type == DetectionType::FIRE ? "FIRE" : "SMOKE",
                                          (float)b.score });
                    }
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
    if (!Actuator_Init("/dev/stm_actuator"))          // STM 액추에이터 보드 (USB) (심볼릭링크)
        std::cerr << "[액추에이터] 초기화 실패 — 계속 진행\n";
    Actuator_Apply(responseForSafe(), "자동:초기화");   // 재시작 후 상태를 알 수 없으므로 평상으로 맞춤 
    QtLink_SetTarget(responseForSafe());                                              
    if (!StmDisplay_Open("/dev/stm_display"))        // STM 전광판 보드 (GPIO UART) (심볼릭링크)
        std::cerr << "[전광판] 초기화 실패 — 계속 진행\n";    
    StmDisplay_SendClear();   // 이전 실행이 대피 화면에서 끝났을 수 있으므로 평상 복귀                
    SpeakerAlert_Stop();      // 이전 실행이 재생 중 종료됐을 수 있으므로 정리   

    AlarmState alarm;
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

    link->stop();
    delete link;
    return 0;
}