#pragma once
#include <string>
#include <vector>
#include "json_util.h"          // JSON 헬퍼 (jsonStr/jsonInt/jsonEscape)
#include "net/link.h"
#include "sensors/sensor_reader.h"
#include "alarm_state.h"
#include "actuator/actuator_control.h"
#include "db/Database.h"

// 명세서 boxes 원소 규격. 감지 코어 타입에 안 묶이게 중립 구조체로
// (화재·연기가 각각 다른 런타임에서 나오는데 한 배열에 합쳐 보내야 함)
struct DetBox {
    int x, y, w, h;
    std::string cls;   // "FIRE" / "SMOKE"
    float score;
};

// 사람 박스. score = 카메라(WiseAI)의 사람 확신도 → Qt가 낮은 값 걸러낼 수 있게
struct PersonBox {
    int x, y, w, h;
    float score;
};

// 서버 관측값. sensor 메시지에 함께 나감                             
// 인자로 하나씩 넘기면 개수가 늘어 순서를 헷갈리므로 묶는다
struct ServerStatus {
    bool sensorOk;        // 센서 값을 믿을 수 있는가
    bool visionOk[4];     // 채널별 서버 영상 감지가 살아있는가
    bool responseOk;      // 목표 대응이 실제 액추에이터에 반영됐는가
    bool clearSensor;     // 해제 체크 — 센서 수치 정상
    bool clearVision;     // 해제 체크 — 영상 감지 정상
    bool clearActuator;   // 해제 체크 — 액추에이터 정상
};  

// ── 서버 → Qt ──
// 명세서 "센서 정보" 스키마
void QtLink_SendSensor(Link& link, const SensorReading& s, const AlarmOutcome& o, const ServerStatus& st);

// 명세서 "카메라 정보" 스키마. 박스 0개여도 전송 (Qt가 오버레이 지움)
void QtLink_SendDetection(Link& link, int ch, int frameId, int srcW, int srcH,
                          bool alarm, const std::vector<DetBox>& boxes);

// 사람 감지. 카메라가 감지, 서버는 좌표 중계만. 판단(state)에는 안 씀
void QtLink_SendPerson(Link& link, int ch, int srcW, int srcH,
                       const std::vector<PersonBox>& persons);

// 명세서 "액추에이터 상태 응답"
void QtLink_SendActuator(Link& link, const ActuatorSnapshot& st);

// 서버가 내린 목표 대응. actuator_status의 target 전송과 responseOk 판정에 쓴다 
// 센서 스레드와 수신 스레드가 같이 보므로 여기서 잠금까지 처리
void QtLink_SetTarget(const Response& r);
Response QtLink_GetTarget();          

// ── Qt → 서버 ──
// 수신 스레드. control / warning_ack 라우팅. 나중에 query도 여기로
void QtLink_RecvWorker(Link& link, AlarmState& alarm, Database& db);