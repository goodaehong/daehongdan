#pragma once
#include <string>
#include "../judgement.h"

// 액추에이터 현재 상태 (명세서 actuator_status 값 그대로)
struct ActuatorSnapshot { int fan; int valve; int siren; };

// 시작 시 1회. UART 포트 열기. 실패해도 서버는 계속 감
bool Actuator_Init(const char* devPath);

// 자동 대응 실행. decideResponse() 결과 그대로 받음 (왜 이 값인지는 몰라도 됨)
// src = 로그 출처 표시용 ("자동:gas" 등), 동작엔 영향 없음
void Actuator_Apply(const Response& r, const std::string& src);

// 수동 제어 (Qt 버튼). target="fan"/"valve"/"siren"
// action: fan="off"/"low"/"mid"/"high", valve="open"/"close", siren="on"/"off"
void Actuator_Execute(const std::string& target, const std::string& action,
                      const std::string& src);

// 지금 상태 꺼내기 (Qt actuator_status 전송용)
ActuatorSnapshot Actuator_GetState();

// 스레드 만들지 말 것. UART 전송은 함수 안에서 동기로 끝낼 것
// 자동(센서 스레드)·수동(수신 스레드) 동시 호출됨 → 이 모듈 안에서 락 (기존 uartMtx가 여기로)