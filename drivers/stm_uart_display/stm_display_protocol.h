/*
 * STM32 display_board와 주고받는 UART 프로토콜을 다른 프로그램(server_main.cpp,
 * 테스트 프로그램 등)에서 공용으로 쓰기 위한 헤더.
 *
 * 여기 정의된 상수/패킷 구조는 stm32_firmware/display_board/Core/Src/main.c의
 * 파서(ProcessRxByte/HandlePacket)와 반드시 일치해야 함 - 한쪽만 고치면 통신 깨짐.
 */
#ifndef STM_DISPLAY_PROTOCOL_H
#define STM_DISPLAY_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CMD 값 (Pi <-> STM32 방향은 함수 이름으로 구분) */
#define STM_DISPLAY_CMD_UPDATE 0x80  /* 평상시 화면 갱신: Pi -> STM32 */
#define STM_DISPLAY_CMD_ALERT  0x90  /* 위험 대피 전환: Pi -> STM32 */
#define STM_DISPLAY_CMD_ACK    0xB0  /* 상태 응답: STM32 -> Pi (지금은 안 읽고 무시해도 됨) */
#define STM_DISPLAY_CMD_CLEAR  0xA0  /* 비상 해제(평상시 화면 복귀): Pi -> STM32 */
#define STM_DISPLAY_CMD_EVAC_PATH 0xB1  /* 대피경로+화재위치: Pi -> STM32 */

/* fireX/fireY에 이 값을 넣으면 "화재 없음" - 대피경로만 표시하고 화재 마커는 안 그림 */
#define STM_DISPLAY_FIRE_NONE 0xFF
/* SendEvacPath의 waypoints 배열 최대 개수 (STM32 packetData[64] 버퍼 기준: (64-4)/2) */
#define STM_DISPLAY_EVAC_MAX_WAYPOINTS 30

/* CMD_UPDATE의 face/gasColor 공통 값 (0=정상/웃음, 1=주의/무표정, 2=위험/찡그림) */
#define STM_DISPLAY_STATE_SAFE    0
#define STM_DISPLAY_STATE_WARNING 1
#define STM_DISPLAY_STATE_DANGER  2

/* CMD_ALERT의 Data1(재난 종류) 값 */
#define STM_DISPLAY_DISASTER_FIRE 0x01
#define STM_DISPLAY_DISASTER_GAS  0x02

/* 이름에 Protocol_이 붙는 이유: server/display/stm_display.h가 서버에서 부르는
   고수준 함수로 StmDisplay_Open/SendUpdate/SendAlert라는 "같은 이름"을 이미 쓰고 있음.
   이 파일은 그 고수준 함수의 내부 구현이 호출하는 저수준(바이트/UART) 계층이라
   이름이 겹치면 헷갈리므로 전부 Protocol_ 접두어로 구분함. */

/* devPath(보통 "/dev/serial0")를 115200 8N1로 열어서 fd 반환. 실패 시 -1.
   실패해도 호출자는 죽을 필요 없음 - 이후 Send 함수들은 fd<0이면 그냥 false만 반환함 */
int StmDisplayProtocol_Open(const char *devPath);

/* USB-시리얼처럼 뽑았다 꽂으면 기존 fd가 죽은 채로 남는 경우 복구용.
   oldFd를 닫고 devPath를 다시 열어서 새 fd 반환 (실패하면 Open과 동일하게 -1).
   Send* 함수가 실패하기 시작하면 호출해서 반환값으로 fd를 교체할 것 */
int StmDisplayProtocol_Reconnect(int oldFd, const char *devPath);

/* 평상시 갱신 패킷(CMD 0x80) 전송. gas는 ppm 값(0~9999), temp/humidity는 정수부만.
   성공하면 true, UART 쓰기 실패하면 false */
bool StmDisplayProtocol_SendUpdate(int fd,
                                    uint8_t face, uint8_t gasColor, uint16_t gas,
                                    uint8_t temp, uint8_t humidity,
                                    uint8_t hour, uint8_t minute,
                                    uint8_t year, uint8_t month, uint8_t day);

/* 위험 대피 전환 패킷(CMD 0x90) 전송. disasterType은 STM_DISPLAY_DISASTER_*,
   zoneId는 1=A구역, 2=B구역, 3=C구역 (프로토콜 문서 기준) */
bool StmDisplayProtocol_SendAlert(int fd, uint8_t disasterType, uint8_t zoneId);

/* 비상 해제 패킷(CMD 0xA0) 전송. 데이터 없음(0바이트) - STM32가 평상시 화면으로 복귀함 */
bool StmDisplayProtocol_SendClear(int fd);

/* 대피경로+화재위치 패킷(CMD 0xB1) 전송.
   fireX/fireY에 STM_DISPLAY_FIRE_NONE(0xFF)을 넣으면 화재 마커 없이 대피경로만 표시됨.
   waypointsXY는 {x0,y0,x1,y1,...} 형태의 평탄화된 배열(길이 = waypointCount*2),
   웨이포인트는 EvacPlanner의 경로 결과처럼 "꺾이는 지점만"이어야 함(STM32가 직선으로 이어 그림).
   waypointCount가 STM_DISPLAY_EVAC_MAX_WAYPOINTS를 넘으면 false(전송 안 함) */
bool StmDisplayProtocol_SendEvacPath(int fd,
                                      uint8_t fireX, uint8_t fireY, uint8_t fireRadius,
                                      const uint8_t *waypointsXY, uint8_t waypointCount);

/* CMD_ACK(0xB0) 응답 대기. STM32는 CMD_UPDATE를 처리하자마자 곧바로 ACK를 보내므로
   SendUpdate 호출 직후에만 의미 있음 (ALERT/CLEAR는 STM32가 ACK를 안 보냄).
   timeoutMs 안에 STX~ETX 프레임을 온전히 못 받으면 false. outStatus에 데이터[0](상태 바이트) 저장,
   필요 없으면 NULL 가능 */
bool StmDisplayProtocol_ReadAck(int fd, int timeoutMs, uint8_t *outStatus);

void StmDisplayProtocol_Close(int fd);

#ifdef __cplusplus
}
#endif

#endif /* STM_DISPLAY_PROTOCOL_H */
