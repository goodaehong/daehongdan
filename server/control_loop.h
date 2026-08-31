#pragma once

// 제어 루프. 1초마다 최신 센서값·감지 상태를 읽어 판단하고,
// 대응 실행 → 기록 → 전송까지 순서대로 한다.
// 판단과 대응 사이에 큐를 두지 않으려고 한 스레드에 모아 둔 것 —
// 위험 판정과 밸브 차단이 어긋나거나 순서가 뒤집히면 안 되기 때문이다.

#include "shared_state.h"
#include "alarm_state.h"
#include "actuator/actuator_control.h"

void controlLoop(Link& link, FrameStore& store, AlarmState& alarm);

// 판단이 정한 대응값 → 액추에이터 입력.
// 제어 루프뿐 아니라 기동 시 평상 상태로 맞출 때도 쓴다
struct Response;
ActuatorCommand toActuatorCommand(const Response& r);
