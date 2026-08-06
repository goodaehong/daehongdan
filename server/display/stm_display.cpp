#include "stm_display.h"
#include "../judgement.h"
#include "../../drivers/stm_uart_display/stm_display_protocol.h"

#include <ctime>

// UART fd는 이 모듈 안에만 보관 (stm_display.h 주석 규칙 - 밖으로 안 꺼냄)
static int s_fd = -1;

bool StmDisplay_Open(const char* devPath)
{
    s_fd = StmDisplayProtocol_Open(devPath);
    return s_fd >= 0;
}

void StmDisplay_Close()
{
    StmDisplayProtocol_Close(s_fd);
    s_fd = -1;
}

// state 문자열 -> face/gasColor 공통 숫자값 (0=정상/웃음, 1=주의/무표정, 2=위험/찡그림)
static uint8_t StateToFaceGasColor(const std::string& state)
{
    if (state == "danger")  return STM_DISPLAY_STATE_DANGER;
    if (state == "warning") return STM_DISPLAY_STATE_WARNING;
    return STM_DISPLAY_STATE_SAFE;
}

bool StmDisplay_SendUpdate(const SensorReading& s, const std::string& state)
{
    uint8_t faceGasColor = StateToFaceGasColor(state);

    // gas: 0~9999로 클램프 (전광판이 4자리까지만 표시 가능)
    float gasClamped = s.gasPpm < 0.0f ? 0.0f : (s.gasPpm > 9999.0f ? 9999.0f : s.gasPpm);
    uint16_t gas = (uint16_t)(gasClamped + 0.5f);

    uint8_t temp = (uint8_t)(s.temp + 0.5f);
    uint8_t humidity = (uint8_t)(s.humidity + 0.5f);

    // 시각은 센서값이 아니라 서버(=STM32에 전달하는 시점)의 현재 시각
    time_t now = time(nullptr);
    tm* lt = localtime(&now);

    return StmDisplayProtocol_SendUpdate(s_fd, faceGasColor, faceGasColor, gas,
                                          temp, humidity,
                                          (uint8_t)lt->tm_hour, (uint8_t)lt->tm_min,
                                          (uint8_t)(lt->tm_year % 100),
                                          (uint8_t)(lt->tm_mon + 1),
                                          (uint8_t)lt->tm_mday);
}

bool StmDisplay_SendAlert(const std::string& cause, int zoneId)
{
    // Cause::Gas만 가스 화면, 나머지(화재/연기/복합 원인)는 전부 화재 화면 (팀 확인 완료)
    uint8_t disasterType = (cause == Cause::Gas) ? STM_DISPLAY_DISASTER_GAS : STM_DISPLAY_DISASTER_FIRE;
    return StmDisplayProtocol_SendAlert(s_fd, disasterType, (uint8_t)zoneId);
}

bool StmDisplay_SendClear()
{
    return StmDisplayProtocol_SendClear(s_fd);
}
