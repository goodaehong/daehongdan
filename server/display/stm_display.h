#pragma once
#include <cstdint>
#include <string>

// 평상시 화면에 올릴 값. 판단·센서 타입을 직접 받지 않고 이 구조체로만 받는다
// (판단 쪽 구조체가 바뀌어도 전광판 코드는 그대로 두려고)
struct DisplayUpdate {
    int   gasLevel;   // 0=정상, 1=주의, 2=위험. 기준값은 판단 계층 소유라 밖에서 정해 넘김
    float gasPpm;     // 클램프·반올림은 이 모듈이
    float temp;
    float humidity;
};

// 대피 화면 종류. 어떤 cause 가 어느 화면인지는 판단 계층이 정한다
enum class DisplayDisaster { Fire, Gas };

// 시작 시 1회. "/dev/serial0" 115200 8N1. 실패해도 서버는 계속 감
bool StmDisplay_Open(const char* devPath);
void StmDisplay_Close();

// 평상시 화면 갱신 (1초 주기)
// 표현 변환은 전부 이 안에서 — float→uint8 반올림, gas 4자리 클램프, 시각 조회
// (화면 표현 바꿔도 호출부는 안 건드리게)
bool StmDisplay_SendUpdate(const DisplayUpdate& u);

// 위험 대피 화면 전환 (위험 진입 시 1회). zoneId 1=A/2=B/3=C/4=D
bool StmDisplay_SendAlert(DisplayDisaster type, int zoneId);

// 위험 해제 → 평상시 화면 복귀
bool StmDisplay_SendClear();

// 대피경로 패킷 전송. 출구 하나당 한 번씩 불러야 함(routeIndex=0부터, EvacPlanner가
// 계산한 출구 순서와 일치해야 함). 주기적으로 계속 부르는 게 아니라 경로가 바뀔 때만 호출할 것.
// waypointsXY는 {x0,y0,x1,y1,...} 평탄화 배열(EvacPlanner의 Point{y,x}를 {x,y}로 바꿔서 넘길 것),
// waypointCount가 STM_DISPLAY_EVAC_MAX_WAYPOINTS(30)를 넘으면 false(전송 안 함).
// 화재 위치는 이 함수와 별개 - StmDisplay_SendEvacFires()를 따로 부를 것
bool StmDisplay_SendEvacPath(uint8_t routeIndex,
                              const uint8_t* waypointsXY, uint8_t waypointCount);

// 화재 위치 목록 전송. firesXYR은 {x0,y0,r0,x1,y1,r1,...} 평탄화 배열(길이=fireCount*3),
// fireCount==0이면 화재 없음(마커 전부 지움). 화재 상황이 바뀔 때만 호출할 것 —
// 대피경로(SendEvacPath)와 달리 출구 개수만큼 반복 호출할 필요 없이 한 번만 부르면 됨.
// fireCount가 STM_DISPLAY_EVAC_MAX_FIRES(6)를 넘으면 false(전송 안 함)
bool StmDisplay_SendEvacFires(const uint8_t* firesXYR, uint8_t fireCount);

// STM32가 마지막 SendUpdate에 대해 ACK(CMD 0xB0)를 보내왔는지. false면 통신 불량으로 간주
// (SendUpdate 안에서 자동으로 짧게 기다렸다 갱신함 - 따로 Poll 호출할 필요 없음)
bool StmDisplay_GetLinkOk();

// fd는 모듈 안에 보관 (밖으로 안 꺼냄)
// STM32와 공유하는 패킷 조립 C 코드는 stm_display_protocol.h로 따로. 이건 서버가 부르는 얼굴
// 스레드 만들지 말 것