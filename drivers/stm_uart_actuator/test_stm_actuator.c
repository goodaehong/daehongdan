/*
 * STM32 actuator_board 수동 테스트 프로그램.
 * 콘솔에 명령어 입력하면 UART로 패킷 전송 + 응답(ACK) 확인.
 *
 * 빌드: gcc test_stm_actuator.c stm_actuator_protocol.c -o test_stm_actuator
 * 실행: sudo ./test_stm_actuator [/dev/serial0 등 UART 경로]
 *
 * 명령어:
 *   fan off | fan low | fan mid | fan high
 *   valve open | valve close
 *   siren on | siren off
 *   status          (CMD_REQ_STATUS 요청 후 응답 파싱해서 출력)
 *   gas_emerg       (CMD_GAS_EMERG)
 *   max_emerg       (CMD_MAX_EMERG)
 *   reset           (CMD_SYS_RESET)
 *   quit
 */

#include "stm_actuator_protocol.h"
 
#include <stdio.h>
#include <string.h>
 
static void print_status(const StmActuatorStatus *s)
{
    const char *fanStr =
        (s->fan == FAN_OFF)  ? "OFF" :
        (s->fan == FAN_LOW)  ? "약"  :
        (s->fan == FAN_MID)  ? "중"  :
        (s->fan == FAN_HIGH) ? "강"  : "?";
 
    printf("  [상태] fan=%s valve=%s siren=%s\n",
           fanStr,
           s->valve == VALVE_OPEN ? "열림" : "닫힘",
           s->siren == SIREN_ON ? "ON" : "OFF");
}

static void wait_and_print_response(int fd, const char *label)
{
    uint8_t cmd;
    StmActuatorStatus status;
    if (StmActuator_ReadResponse(fd, 1000, &cmd, &status))
    {
        printf("[%s] 응답 수신 cmd=0x%02X\n", label, cmd);
        if (cmd == CMD_REQ_STATUS)
        {
            print_status(&status);
        }
    }
    else
    {
        printf("[%s] 응답 없음 (timeout 1초 - STM32 연결/전원 확인 필요)\n", label);
    }
}
 
int main(int argc, char *argv[])
{
    const char *devPath = (argc > 1) ? argv[1] : "/dev/serial0";
 
    int fd = StmActuator_Open(devPath);
    if (fd < 0)
    {
        perror("UART open 실패");
        return 1;
    }
 
    printf("STM32 actuator 테스트 (%s)\n", devPath);
    printf("명령어: fan off/low/mid/high, valve open/close, siren on/off,\n");
    printf("        status, gas_emerg, max_emerg, reset, quit\n");
 
    char line[64];
    while (fgets(line, sizeof(line), stdin))
    {
        line[strcspn(line, "\n")] = '\0';
 
        if (strcmp(line, "quit") == 0)
        {
            break;
        }
        else if (strncmp(line, "fan ", 4) == 0)
        {
            const char *arg = line + 4;
            uint8_t speed;
            if      (strcmp(arg, "off")  == 0) speed = FAN_OFF;
            else if (strcmp(arg, "low")  == 0) speed = FAN_LOW;
            else if (strcmp(arg, "mid")  == 0) speed = FAN_MID;
            else if (strcmp(arg, "high") == 0) speed = FAN_HIGH;
            else { printf("알 수 없는 fan 값: %s\n", arg); continue; }
 
            bool ok = StmActuator_SendFan(fd, speed);
            printf("fan %s 전송 %s\n", arg, ok ? "성공" : "실패");
            wait_and_print_response(fd, "fan");
        }
        else if (strncmp(line, "valve ", 6) == 0)
        {
            const char *arg = line + 6;
            uint8_t state;
            if      (strcmp(arg, "open")  == 0) state = VALVE_OPEN;
            else if (strcmp(arg, "close") == 0) state = VALVE_CLOSED;
            else { printf("알 수 없는 valve 값: %s\n", arg); continue; }
 
            bool ok = StmActuator_SendValve(fd, state);
            printf("valve %s 전송 %s\n", arg, ok ? "성공" : "실패");
            wait_and_print_response(fd, "valve");
        }
        else if (strncmp(line, "siren ", 6) == 0)
        {
            const char *arg = line + 6;
            uint8_t state;
            if      (strcmp(arg, "on")  == 0) state = SIREN_ON;
            else if (strcmp(arg, "off") == 0) state = SIREN_OFF;
            else { printf("알 수 없는 siren 값: %s\n", arg); continue; }
 
            bool ok = StmActuator_SendSiren(fd, state);
            printf("siren %s 전송 %s\n", arg, ok ? "성공" : "실패");
            wait_and_print_response(fd, "siren");
        }
        else if (strcmp(line, "status") == 0)
        {
            bool ok = StmActuator_SendReqStatus(fd);
            printf("status 요청 전송 %s\n", ok ? "성공" : "실패");
            wait_and_print_response(fd, "status");
        }
        else if (strcmp(line, "gas_emerg") == 0)
        {
            bool ok = StmActuator_SendGasEmerg(fd);
            printf("gas_emerg 전송 %s\n", ok ? "성공" : "실패");
            wait_and_print_response(fd, "gas_emerg");
        }
        else if (strcmp(line, "max_emerg") == 0)
        {
            bool ok = StmActuator_SendMaxEmerg(fd);
            printf("max_emerg 전송 %s\n", ok ? "성공" : "실패");
            wait_and_print_response(fd, "max_emerg");
        }
        else if (strcmp(line, "reset") == 0)
        {
            bool ok = StmActuator_SendSysReset(fd);
            printf("reset 전송 %s\n", ok ? "성공" : "실패");
            wait_and_print_response(fd, "reset");
        }
        else if (line[0] != '\0')
        {
            printf("알 수 없는 명령어: %s\n", line);
        }
    }
 
    StmActuator_Close(fd);
    return 0;
}