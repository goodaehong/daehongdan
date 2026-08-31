// camera_worker.h 구현. 채널별 RTSP 수신 · 감지 제출 · 결과 전송을 담당한다.

#include "camera_worker.h"

#include <opencv2/opencv.hpp>
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "detection/FireDetectionRuntime.h"
#include "detection/PersonMetadataReceiver.h"
#include "detection/AppConfig.h"
#include "qt_link.h"
#include "roi/roi_store.h"
#include "clip/clip_recorder.h"
#include "calib/calib_store.h"
#include "calib/aruco_config.h"

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
void cameraWorker(int ch, FrameStore& store, Link& link, SmokeDetectionRuntime& smoke) {
    std::string url = std::string(RTSP_BASE_URL) + std::to_string(ch + 1);

    PersonMetadataReceiver person;
    person.start(url);              // 카메라 WiseAI 사람 메타데이터 수신 (FFmpeg)

    FireDetectionRuntime runtime;   // 복사 금지 타입 → 채널당 지역변수 1개
    loadFactoryMapping(runtime, ch);   // ArUco 보정 로드. 없으면 좌표만 비활성
    int roiVer = -1;  // ROI 설정 버전. 바뀔 때만 런타임에 반영
    int calibVer = CalibStore_ReloadVersion(ch);   // Qt 재로드 요청 감시용   
    std::uint64_t frameId = 0;
    bool wasShowingBoxes = false;
    bool prevSmoke = false, prevPerson = false, prevFire = false;   // 로그: 변화 시에만 출력용
    // 연기는 약 1fps, 화재는 약 6fps. 화재 결과가 올 때마다 박스 목록을 통째로
    // 교체하면 그 사이 연기 박스가 빠진다. 마지막 연기 박스를 들고 있다가 같이 보낸다
    std::vector<DetBox> lastSmokeBoxes;                           
    std::string prevPersonStatus;   // 사람 메타데이터 상태. 바뀔 때만 로그

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
            // 이 채널이 지금 ArUco 보정 중이면 서버 캡처를 내려놓는다. 보정 도구가
            // 같은 RTSP 스트림(rtsp://localhost:8554/camN)에 독립적으로 접속하는데,
            // 서버까지 같이 그 스트림을 물고 있으면 파이에서 리소스 경합이 나서
            // 프레임이 깨지고(h264 디코드 에러) 보정이 계속 실패한다.
            if (ArucoConfig_IsRunning(ch)) {
                std::cout << "[cam" << ch + 1 << "] 보정 진행 중 — 서버 캡처 일시 중단\n";
                break;
            }
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

            // 결과가 없는 프레임에도 false가 되어 감지/해제 로그가 1초마다 반복됐다.
            // 위에서 이미 보호해 둔 상태값을 그대로 쓴다
            bool nowSmoke = detState[ch].smoke;             
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

            // 메타데이터 수신 상태가 바뀔 때만 찍는다. 이번 ffmpeg 옵션 문제처럼    
            // 조용히 실패하면 count:0만 계속 나가서 원인을 알 수 없다
            if (pf.status != prevPersonStatus) {
                prevPersonStatus = pf.status;
                std::cout << "[cam" << ch+1 << "] 사람 메타데이터 — " << pf.status << "\n";
            }                                                                      

            // 사람 좌표 전송 — 매 프레임은 과함. 0.5초 간격(30fps 기준)
            if (frameId % 15 == 0) {
                std::vector<PersonBox> persons;
                for (const auto& p : pf.persons)
                    persons.push_back({ p.box.x, p.box.y, p.box.width, p.box.height,
                                        (float)p.confidence });
                QtLink_SendPerson(link, ch, frame.cols, frame.rows, persons);
            }

            // ── 화재 + 연기 박스 전송 ──                                 
            const bool channelDetectionAlarm =
                snap.alarm.alarmActive || detState[ch].smoke;

            // 새 연기 결과가 도착했을 때 캐시를 갱신한다. 단일 NCNN 워커가
            // 네 채널을 순차 처리하는 동안에는 직전 박스를 계속 Qt로 보낸다.
            if (ssnap.boxIsFresh) {
                lastSmokeBoxes.clear();
                for (const auto& b : ssnap.detection.boxes)
                    lastSmokeBoxes.push_back({ b.box.x, b.box.y, b.box.width, b.box.height,
                                               "SMOKE", (float)b.score });
            }
            if (!detState[ch].smoke)
                lastSmokeBoxes.clear();

            const bool hasCachedSmokeBoxes =
                detState[ch].smoke && !lastSmokeBoxes.empty();
            if (snap.boxIsFresh || hasCachedSmokeBoxes) {
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
                    {   // 감지 스레드가 쓰고 센서 스레드가 읽는다                 
                        std::lock_guard<std::mutex> lk(detState[ch].fireMtx);
                        detState[ch].fires = std::move(fires);
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
                boxes.insert(boxes.end(), lastSmokeBoxes.begin(), lastSmokeBoxes.end());  

                QtLink_SendDetection(link, ch, (int)snap.resultFrameId,
                                     frame.cols, frame.rows,
                                     channelDetectionAlarm, boxes);
                wasShowingBoxes = true;
            }                                                             
            else if (wasShowingBoxes && !channelDetectionAlarm) {
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

        // 보정이 끝날 때까지 재연결을 미룬다 — 안 그러면 3초마다 재접속을 시도하면서
        // 다시 스트림 경합을 일으킨다.
        if (ArucoConfig_IsRunning(ch)) {
            while (ArucoConfig_IsRunning(ch))
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            std::cout << "[cam" << ch + 1 << "] 보정 종료 — 서버 캡처 재개\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}
