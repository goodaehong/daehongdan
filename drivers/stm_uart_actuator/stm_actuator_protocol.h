#ifndef STM_ACTUATOR_PROTOCOL_H
#define STM_ACTUATOR_PROTOCOL_H
 
#include <stdint.h>
#include <stdbool.h>
 
#ifdef __cplusplus
extern "C" {
#endif

/*
 * 서버(라즈베리파이) <-> STM32 actuator_board UART 프로토콜
 * (태호 팀원 명세 기준)
 *
 * 서버 -> STM32 : [STX] [Length] [Command] [Data...] [Checksum] [ETX]
 * STM32 -> 서버 : [STX] [Length] [Command] [Data...] [Checksum] [ETX]
 *   (요청 ACK는 Length=0, 상태 응답(CMD_REQ_STATUS)은 Length=3 + Data 3바이트)
 *
 * Checksum = Command와 Data 바이트를 모두 XOR한 값
 *   (Length 포함 여부는 태호 STM32 파싱 코드와 반드시 재확인 필요.
 *    여기서는 "Command + Data만 XOR"로 구현함 - 명세 문구 그대로.)
 */

// 패킷의 시작(STX)과 끝(ETX)을 알리는 특수 바이트
#define STM_ACTUATOR_STX 0x02
#define STM_ACTUATOR_ETX 0x03
 
/* ── Command ID ── */
#define CMD_FAN_CTRL     0x10  /* Data: 0x00 OFF, 0x01 약, 0x02 중, 0x03 강 */
#define CMD_VALVE_CTRL   0x20  /* Data: 0x00 닫힘, 0x01 열림 */
#define CMD_SIREN_CTRL   0x30  /* Data: 0x00 OFF, 0x01 ON */
#define CMD_REQ_STATUS   0x40  /* Data 없음 (요청) / 응답 시 Data 3바이트 */
#define CMD_GAS_EMERG    0x50  /* Data 없음: 사이렌 ON + 밸브 즉시 차단 + 팬 강 */
#define CMD_MAX_EMERG    0x60  /* Data 없음: 사이렌 ON + 밸브 차단 + 팬 OFF */
#define CMD_SYS_RESET    0x70  /* Data 없음: 사이렌 OFF + 밸브 오픈 + 팬 약 */

/* ── Fan 속도 값 ── */
#define FAN_OFF  0x00
#define FAN_LOW  0x01
#define FAN_MID  0x02
#define FAN_HIGH 0x03
 
/* ── Valve 상태 값 ── */
#define VALVE_CLOSED 0x00
#define VALVE_OPEN   0x01
 
/* ── Siren 상태 값 ── */
#define SIREN_OFF 0x00
#define SIREN_ON  0x01

/* STM32 -> 서버 상태 응답 파싱 결과 (CMD_REQ_STATUS 응답용) */
typedef struct {
    uint8_t fan;    /* FAN_OFF/LOW/MID/HIGH */
    uint8_t valve;  /* VALVE_CLOSED/OPEN */
    uint8_t siren;  /* SIREN_OFF/ON */
} StmActuatorStatus;
/* 이름에 Protocol_이 붙는 이유: server/actuator/actuator_control.h가 서버에서 부르는
   고수준 함수로 Actuator_Init/Execute/Apply/Poll이라는 이름을 이미 쓰고 있음.
   이 파일은 그 고수준 함수의 내부 구현이 호출하는 저수준(바이트/UART) 계층이라
   이름이 겹치지 않게 전부 Protocol_ 접두어로 구분함 (drivers/stm_uart_display와 동일 규칙). */

/* UART 오픈 (115200 8N1, raw 모드). 실패 시 -1 */
int StmActuatorProtocol_Open(const char *devPath);
void StmActuatorProtocol_Close(int fd);

/* ── 개별 명령 전송 (전송 성공 여부만 반환, ACK 대기는 안 함) ── */
bool StmActuatorProtocol_SendFan(int fd, uint8_t fanSpeed);
bool StmActuatorProtocol_SendValve(int fd, uint8_t valveState);
bool StmActuatorProtocol_SendSiren(int fd, uint8_t sirenState);
bool StmActuatorProtocol_SendReqStatus(int fd);
bool StmActuatorProtocol_SendGasEmerg(int fd);
bool StmActuatorProtocol_SendMaxEmerg(int fd);
bool StmActuatorProtocol_SendSysReset(int fd);

/*
 * STM32로부터 응답 1프레임을 읽어서 파싱.
 * timeoutMs 안에 STX~ETX 완전한 프레임을 못 받으면 false.
 * CMD_REQ_STATUS 응답(Len=3)이면 outStatus에 값 채움, outCmd에 0x40 세팅.
 * 그 외 ACK(Len=0)면 outCmd에 해당 커맨드만 세팅되고 outStatus는 안 건드림.
 */
bool StmActuatorProtocol_ReadResponse(int fd, int timeoutMs, uint8_t *outCmd, StmActuatorStatus *outStatus);
 
#ifdef __cplusplus
}
#endif
 
#endif /* STM_ACTUATOR_PROTOCOL_H */