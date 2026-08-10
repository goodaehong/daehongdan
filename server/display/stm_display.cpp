#include "stm_display.h"
#include "../judgement.h"
#include "../../drivers/stm_uart_display/stm_display_protocol.h"

#include <ctime>

// UART fd는 이 모듈 안에만 보관 (stm_display.h 주석 규칙 - 밖으로 안 꺼냄)
static int s_fd = -1;
static std::string s_devPath;
static bool s_linkOk = false;

// STM32가 UPDATE를 받자마자 바로 ACK를 쏘므로 짧게만 기다리면 됨.
// 너무 길게 잡으면 SendUpdate가 1초 주기 루프를 블로킹해버림
static const int kAckTimeoutMs = 200;

bool StmDisplay_Open(const char* devPath)
{
    s_devPath = devPath;
    s_fd = StmDisplayProtocol_Open(devPath);
    return s_fd >= 0;
}

// USB-시리얼처럼 케이블을 뽑았다 꽂으면 기존 fd가 죽은 채로 남아서 재부팅 전까지
// 계속 전송 실패만 남는 문제가 있었음 - Send*가 실패하면 다음 시도 전에 재연결해둠
static void ReconnectIfBroken(bool sent)
{
    if (!sent && !s_devPath.empty())
    {
        s_fd = StmDisplayProtocol_Reconnect(s_fd, s_devPath.c_str());
    }
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

    bool sent = StmDisplayProtocol_SendUpdate(s_fd, faceGasColor, faceGasColor, gas,
                                               temp, humidity,
                                               (uint8_t)lt->tm_hour, (uint8_t)lt->tm_min,
                                               (uint8_t)(lt->tm_year % 100),
                                               (uint8_t)(lt->tm_mon + 1),
                                               (uint8_t)lt->tm_mday);

    // ACK 수신 여부는 통신 상태 트래킹용일 뿐 - 여기서 실패해도 SendUpdate 자체 성공/실패(sent)와는 별개로 취급
    uint8_t ackStatus = 0;
    s_linkOk = sent && StmDisplayProtocol_ReadAck(s_fd, kAckTimeoutMs, &ackStatus);

    ReconnectIfBroken(sent);
    return sent;
}

bool StmDisplay_GetLinkOk()
{
    return s_linkOk;
}

bool StmDisplay_SendAlert(const std::string& cause, int zoneId)
{
    // Cause::Gas만 가스 화면, 나머지(화재/연기/복합 원인)는 전부 화재 화면 (팀 확인 완료)
    uint8_t disasterType = (cause == Cause::Gas) ? STM_DISPLAY_DISASTER_GAS : STM_DISPLAY_DISASTER_FIRE;
    bool sent = StmDisplayProtocol_SendAlert(s_fd, disasterType, (uint8_t)zoneId);
    ReconnectIfBroken(sent);
    return sent;
}

bool StmDisplay_SendClear()
{
    bool sent = StmDisplayProtocol_SendClear(s_fd);
    ReconnectIfBroken(sent);
    return sent;
}
