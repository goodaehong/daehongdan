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
#include <csignal>

#include "detection/FireDetectionRuntime.h"
#include "detection/SmokeDetectionRuntime.h"
#include "detection/PersonMetadataReceiver.h"
#include "detection/AppConfig.h"

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
#include "shared_state.h"
#include "camera_worker.h"
#include "control_loop.h"
#include "sensor_worker.h"
#include "audio/speaker_alert.h"   
#include "clip/clip_recorder.h"    
#include "calib/calib_store.h"    
#include "calib/aruco_config.h"

// 보정 계산 스레드 → Qt 결과 전송. ArucoConfig 가 계산을 끝내면 부른다  
static void onCalibRunDone(int ch, CalibRunResult r, const std::string& detail) { 
    // 성공하면 사람이 "재로드"를 또 누르지 않아도 바로 반영되게 한다       
    if (r == CalibRunResult::Ok) CalibStore_RequestReload(ch - 1);
    if (g_link) QtLink_SendCalibRunDone(*g_link, ch, r, detail);  
}                                                             

int main() {
    // Qt가 정상 종료 절차 없이 창을 바로 닫으면(연결이 이미 끊긴 소켓에 SSL_write하게 됨),
    // SIGPIPE 기본 동작이 프로세스 즉시 종료라서 서버 전체가 아무 로그도 없이 죽어버린다.
    // 여기서 무시해두면 write()가 그냥 -1/EPIPE를 반환하고, txThreadLoop의 기존
    // "bytes <= 0 → 송신 실패 로그 + 다음 연결 대기" 경로로 정상 처리된다.
    std::signal(SIGPIPE, SIG_IGN);

    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS",
           "rtsp_transport;tcp|fflags;nobuffer|flags;low_delay", 1);   // 저지연 옵션
    cv::setNumThreads(1);   // OpenCV 채널당 1스레드 = 멀티채널 최적화 핵심

    // ── 초기화. 하나 실패해도 나머지는 계속 감 ──
    Link* link = CreateLink();
    if (!link->start(QT_LINK_PORT)) {
        std::cerr << "[링크] 시작 실패\n";
        return 1;
    }
    if (!g_db.open(DB_PATH))
        std::cerr << "[DB] 초기화 실패 — DB 없이 계속 진행\n";
    // 녹화가 끝나는 건 13초 뒤 저장 스레드다. 그때 DB 행에 경로만 채워 넣는다   
    ClipRecorder_Init([](long incidentId, long ts, const std::string& path) {
        g_db.updateClipPath(incidentId, ts, path);
    });                            
    g_link = link;                        // 계산 완료 알림에서 쓴다        
    ArucoConfig_SetOnDone(onCalibRunDone);                                                                                                                           
    RoiStore_Load();   // 저장된 ROI 복원. 파일 없으면 빈 상태로 시작  
    FloorMapStore_Load();   // 저장된 평면도 변환 결과 복원 
    if (!Actuator_Init(ACTUATOR_DEVICE))          // STM 액추에이터 보드 (USB) (심볼릭링크)
        std::cerr << "[액추에이터] 초기화 실패 — 계속 진행\n";
    Actuator_Apply(toActuatorCommand(responseForSafe()), "자동:초기화");   // 재시작 후 상태를 알 수 없으므로 평상으로 맞춤 
    QtLink_SetTarget(responseForSafe());                                              
    if (!StmDisplay_Open(STM_DISPLAY_DEVICE))        // STM 전광판 보드 (GPIO UART) (심볼릭링크)
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
    std::thread sensorThread(sensorWorker);
    sensorThread.detach();

    std::thread controlThread(controlLoop, std::ref(*link), std::ref(store), std::ref(alarm));
    controlThread.detach();

    std::thread recvThread(QtLink_RecvWorker, std::ref(*link), std::ref(alarm), std::ref(g_db));
    recvThread.detach();

    std::thread cams[4];
    for (int i = 0; i < 4; i++)
        cams[i] = std::thread(cameraWorker, i, std::ref(store), std::ref(*link), std::ref(smoke));
    for (int i = 0; i < 4; i++)
        cams[i].join();
    
    ClipRecorder_Shutdown();   // 녹화 중이던 것도 모인 데까지 저장하고 스레드 정리  
    ArucoConfig_Shutdown();    // 계산 중인 스레드 마무리 
    link->stop();
    delete link;
    return 0;
}
