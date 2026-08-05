#pragma once
#include <string>
#include "../sensors/sensor_reader.h"

// 시작 시 1회. "/dev/serial0" 115200 8N1. 실패해도 서버는 계속 감
bool StmDisplay_Open(const char* devPath);
void StmDisplay_Close();

// 평상시 화면 갱신 (1초 주기). state = "safe"/"warning"/"danger"
// 변환은 전부 이 안에서 — state→face/gasColor, float→uint8 반올림, gas 4자리 클램프, 시각 조회
// (화면 표현 바꿔도 server_main 안 건드리게)
bool StmDisplay_SendUpdate(const SensorReading& s, const std::string& state);

// 위험 대피 화면 전환 (위험 진입 시 1회). cause는 Cause:: 값, zoneId 1=A/2=B/3=C/4=D
bool StmDisplay_SendAlert(const std::string& cause, int zoneId);

// 위험 해제 → 평상시 화면 복귀
bool StmDisplay_SendClear();

// fd는 모듈 안에 보관 (밖으로 안 꺼냄)
// STM32와 공유하는 패킷 조립 C 코드는 stm_display_protocol.h로 따로. 이건 서버가 부르는 얼굴
// 스레드 만들지 말 것