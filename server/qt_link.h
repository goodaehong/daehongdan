#pragma once
#include <string>
#include <vector>
#include "net/link.h"
#include "sensors/sensor_reader.h"
#include "alarm_state.h"
#include "actuator/actuator_control.h"
#include "db/Database.h"

// JSON 필드 추출 헬퍼 (라이브러리 없이 문자열 검색으로)
std::string jsonStr(const std::string& j, const std::string& key);
int         jsonInt(const std::string& j, const std::string& key, int def);

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

// ── 서버 → Qt ──
// 명세서 "센서 정보" 스키마
void QtLink_SendSensor(Link& link, const SensorReading& s, const AlarmOutcome& o);

// 명세서 "카메라 정보" 스키마. 박스 0개여도 전송 (Qt가 오버레이 지움)
void QtLink_SendDetection(Link& link, int ch, int frameId, int srcW, int srcH,
                          bool alarm, const std::vector<DetBox>& boxes);

// 사람 감지. 카메라가 감지, 서버는 좌표 중계만. 판단(state)에는 안 씀
void QtLink_SendPerson(Link& link, int ch, int srcW, int srcH,
                       const std::vector<PersonBox>& persons);

// 명세서 "액추에이터 상태 응답"
void QtLink_SendActuator(Link& link, const ActuatorSnapshot& st);

// ── Qt → 서버 ──
// 수신 스레드. control / warning_ack 라우팅. 나중에 query도 여기로
void QtLink_RecvWorker(Link& link, AlarmState& alarm, Database& db);