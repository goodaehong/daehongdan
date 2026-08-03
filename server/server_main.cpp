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

// 4채널 프레임 공유 저장소. 채널별 mutex로 워커/센서 스레드 간 경합 방지
struct FrameStore {
    cv::Mat frames[4];
    std::mutex mtx[4];
};

// 채널별 최신 감지 상태. 워커가 갱신, 판단(센서 스레드)이 읽음
struct DetectionState {
    std::atomic<bool> fire{false};
    std::atomic<bool> smoke{false};
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
    const std::string dir = "db/snapshots";
    ::mkdir("db", 0755);
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

// ── 센서 스레드: 1초마다 읽기 → 판단 → 대응 → 기록 → 전송 ──
void sensorWorker(Link& link, FrameStore& store, AlarmState& alarm) {
    int tick = 0;

    while (true) {
        SensorReading s;
        if (!SensorReader_Read(s)) {   // 실패 시 판단 건너뜀 (0을 "안전"으로 오판 방지)
            std::cerr << "[센서] 읽기 실패 — 이번 주기 건너뜀\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
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

        long now = std::time(nullptr);
        AlarmOutcome o = alarm.update(judgeState(camFire, camSmoke, s), now);

        // 경고 진입 = 기록 + 스냅샷 (액추에이터 대응은 없음)
        if (o.warnEntered) {
            std::string snap = saveSnapshot(store, detCh, "A", now);
            g_db.insertEvent(now, "A", "warning", o.j.state, o.j.cause,
                             causeToCombo(o.j.cause), "auto", "", "",
                             s.gasPpm, s.smokePpm, "진행중", 0, snap, 0, "");
        }

        // 위험 진입 또는 원인 변경 = 자동 대응 + 전광판 + 기록
        if (o.dangerEntered) {
            std::string src = "자동:" + o.j.cause;
            Response r = decideResponse(o.j.cause);
            Actuator_Apply(r, src);
            QtLink_SendActuator(link, Actuator_GetState());
            StmDisplay_SendAlert(o.j.cause, 1);

            std::string snap = saveSnapshot(store, detCh, "A", now);
            g_db.insertEvent(now, "A", "danger", o.j.state, o.j.cause,
                             causeToCombo(o.j.cause), "auto", respToText(r), "",
                             s.gasPpm, s.smokePpm, "진행중", 0, snap, o.incidentId, "");
        }

        // 위험 해제 = 복귀 대응 + 지속시간 확정
        if (o.released) {
            Actuator_Apply(responseForSafe(), "자동:해제");
            QtLink_SendActuator(link, Actuator_GetState());
            StmDisplay_SendClear();

            g_db.resolveIncident(o.incidentId, o.durationMs);   // 진행중→해결됨 + 지속시간 일괄
            g_db.insertEvent(now, "A", "resolve", "safe", "", "", "auto", "위험 해제", "",
                             s.gasPpm, s.smokePpm, "해결됨", 0, "", o.incidentId, "");
        }

        QtLink_SendSensor(link, s, o);
        StmDisplay_SendUpdate(s, o.j.state);
        g_db.insertSensor(now, "A", s.temp, s.humidity, s.gasPpm, s.smokePpm, s.flameVal, o.j.state);

        // 액추에이터 상태 주기 보고: Qt가 새로 접속해도 화면 동기화되게
        if (++tick % 5 == 0) QtLink_SendActuator(link, Actuator_GetState());

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

            // ── 화재 박스 전송 ──
            if (snap.boxIsFresh) {
                bool hasFire = false;
                std::vector<DetBox> boxes;
                for (const auto& b : snap.detection.boxes) {
                    if (b.type == DetectionType::FIRE) hasFire = true;
                    boxes.push_back({ b.box.x, b.box.y, b.box.width, b.box.height,
                                      b.type == DetectionType::FIRE ? "FIRE" : "SMOKE",
                                      b.score });
                }
                // TODO: 연기(NCNN) 박스도 여기 boxes에 push_back (cls="SMOKE")

                detState[ch].fire = hasFire && snap.alarm.alarmActive;

                bool nowFire = detState[ch].fire;   // 화재 로그: 확정/해제 순간만
                if (nowFire && !prevFire)
                    std::cout << "[cam" << ch+1 << "] 화재 감지 (알람 확정)\n";
                else if (!nowFire && prevFire)
                    std::cout << "[cam" << ch+1 << "] 화재 해제\n";
                prevFire = nowFire;

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
    if (!Actuator_Init("/dev/serial0"))
        std::cerr << "[액추에이터] 초기화 실패 — 계속 진행\n";
    if (!StmDisplay_Open("/dev/serial1"))
        std::cerr << "[전광판] 초기화 실패 — 계속 진행\n";

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