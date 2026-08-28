#include "stm_display.h"
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

bool StmDisplay_SendUpdate(const DisplayUpdate& u)
{
    // 표정/가스 그래프 둘 다 종합 판정이 아니라 가스 농도로만 결정된다.
    // 화재/연기만 감지되고 가스는 정상이어도 표정·그래프가 빨갛게 뜨던 문제 때문
    // (화재/연기 경보는 SendAlert의 전용 화면으로 따로 처리됨)
    uint8_t face = (uint8_t)u.gasLevel;
    uint8_t gasColor = face;

    // gas: 0~9999로 클램프 (전광판이 4자리까지만 표시 가능)
    float gasClamped = u.gasPpm < 0.0f ? 0.0f : (u.gasPpm > 9999.0f ? 9999.0f : u.gasPpm);
    uint16_t gas = (uint16_t)(gasClamped + 0.5f);

    uint8_t temp = (uint8_t)(u.temp + 0.5f);
    uint8_t humidity = (uint8_t)(u.humidity + 0.5f);

    // 시각은 센서값이 아니라 서버(=STM32에 전달하는 시점)의 현재 시각
    time_t now = time(nullptr);
    tm* lt = localtime(&now);

    bool sent = StmDisplayProtocol_SendUpdate(s_fd, face, gasColor, gas,
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

bool StmDisplay_SendAlert(DisplayDisaster type, int zoneId)
{
    uint8_t disasterType = (type == DisplayDisaster::Gas) ? STM_DISPLAY_DISASTER_GAS
                                                          : STM_DISPLAY_DISASTER_FIRE;
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

bool StmDisplay_SendEvacPath(uint8_t routeIndex,
                              const uint8_t* waypointsXY, uint8_t waypointCount)
{
    bool sent = StmDisplayProtocol_SendEvacPath(s_fd, routeIndex, waypointsXY, waypointCount);
    ReconnectIfBroken(sent);
    return sent;
}

bool StmDisplay_SendEvacFires(const uint8_t* firesXYR, uint8_t fireCount)
{
    bool sent = StmDisplayProtocol_SendEvacFires(s_fd, firesXYR, fireCount);
    ReconnectIfBroken(sent);
    return sent;
}
