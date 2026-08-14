/*
 * 실제 센서값(server/sensors/의 완성된 DHT22·ADS1115 읽기 코드를 그대로 재사용)을
 * 1초마다 STM32 전광판으로 평상시 갱신(CMD 0x80)하면서, 콘솔에 "1"/"2"를 입력해
 * 위험 전환(CMD 0x90)/평상시 복귀(CMD 0xA0)를 수동으로 테스트하는 프로그램.
 *
 * server_main.cpp(judgement/알람 로직)는 안 쓰고, 센서 읽기 코드만 가져다 씀 —
 * 실제 자동 판단 없이 순수하게 "UART 패킷이 잘 오가는지"만 확인하려는 목적.
 *
 * 사전 준비: server/sensors/sensor_reader_hw.cpp가 읽는 드라이버(dht22, gas_sensor)가
 *           로드되어 있어야 함 (drivers/README.md 체크리스트 참고).
 *
 * 빌드 (drivers/stm_uart_display/ 안에서):
 *   g++ -std=c++17 test_alert_uart.cpp \
 *       ../../server/sensors/sensor_reader_hw.cpp \
 *       ../../server/sensors/sensor_conversion.cpp \
 *       stm_display_protocol.c \
 *       -I../../server/sensors -o test_alert_uart
 *
 * 실행: sudo ./test_alert_uart   (UART 권한 문제 있으면 sudo)
 *   콘솔에 1 입력+엔터 -> 위험(화재) 전환 화면
 *   콘솔에 2 입력+엔터 -> 평상시 화면 복귀
 */
#include "sensor_reader.h"
#include "stm_display_protocol.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

// 로그 줄 맨 앞에 붙일 "HH:MM:SS" - std::put_time은 tm 값을 그대로 참조하므로
// 매 호출 시점의 로컬 시각으로 새로 계산해서 반환
static std::string CurrentTimeString()
{
    time_t now = time(nullptr);
    tm* lt = localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(lt, "%H:%M:%S");
    return oss.str();
}

// USB-시리얼처럼 케이블을 뽑았다 꽂으면 처음 연 fd가 죽은 채로 남아서, 재연결 전까지는
// 계속 전송 실패만 남음(프로그램 재시작 없이는 절대 안 살아남). g_fd는 메인 루프와
// InputWorker 스레드가 같이 쓰므로 atomic으로 공유해서 재연결 시 양쪽 다 새 fd를 보게 함
static std::atomic<int> g_fd{-1};
static const char* kDevPath = "/dev/stm_display";

static void InputWorker()
{
    std::cout << "[테스트] '1' 입력+엔터 = 위험(화재) 전환, '2' 입력+엔터 = 평상시 복귀\n";
    std::string line;
    while (std::getline(std::cin, line))
    {
        int fd = g_fd.load();
        if (line == "1")
        {
            bool ok = StmDisplayProtocol_SendAlert(fd, STM_DISPLAY_DISASTER_FIRE, 0x01);
            std::cout << "[테스트] 위험 전환 패킷(CMD 0x90) 전송 " << (ok ? "성공" : "실패") << "\n";
            if (!ok) { g_fd = StmDisplayProtocol_Reconnect(fd, kDevPath); }
        }
        else if (line == "2")
        {
            bool ok = StmDisplayProtocol_SendClear(fd);
            std::cout << "[테스트] 평상시 복귀 패킷(CMD 0xA0) 전송 " << (ok ? "성공" : "실패") << "\n";
            if (!ok) { g_fd = StmDisplayProtocol_Reconnect(fd, kDevPath); }
        }
        else if (!line.empty())
        {
            std::cout << "[테스트] '1' 또는 '2'만 입력 가능\n";
        }
    }
}

int main()
{
    g_fd = StmDisplayProtocol_Open(kDevPath);
    if (g_fd.load() < 0)
    {
        std::cerr << "UART 열기 실패 (" << kDevPath << ")\n";
        return 1;
    }

    std::thread inputThread(InputWorker);
    inputThread.detach();

    while (true)
    {
        SensorReading s;
        if (!SensorReader_Read(s))
        {
            std::cerr << "[" << CurrentTimeString() << "] [센서] 읽기 실패, 이번 주기 건너뜀\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        // 평상시 갱신 패킷의 표정/가스색도 실제 가스 ppm 기준으로 계산해서 보냄
        // (전환화면 표시 중엔 STM32가 이 0x80 패킷을 무시하게 되어있어서, 1/2 입력으로
        // 보내는 CMD 0x90/0xA0랑은 서로 안 부딪힘)
        uint8_t faceGasColor;
        if (s.gasPpm >= 2001.0f)      faceGasColor = STM_DISPLAY_STATE_DANGER;
        else if (s.gasPpm >= 201.0f)  faceGasColor = STM_DISPLAY_STATE_WARNING;
        else                          faceGasColor = STM_DISPLAY_STATE_SAFE;

        float gasClamped = s.gasPpm < 0.0f ? 0.0f : (s.gasPpm > 9999.0f ? 9999.0f : s.gasPpm);
        uint16_t gas = (uint16_t)(gasClamped + 0.5f);
        uint8_t temp = (uint8_t)(s.temp + 0.5f);
        uint8_t humidity = (uint8_t)(s.humidity + 0.5f);

        time_t now = time(nullptr);
        tm* lt = localtime(&now);

        int fd = g_fd.load();
        bool ok = StmDisplayProtocol_SendUpdate(fd, faceGasColor, faceGasColor, gas,
                                                 temp, humidity,
                                                 (uint8_t)lt->tm_hour, (uint8_t)lt->tm_min,
                                                 (uint8_t)(lt->tm_year % 100),
                                                 (uint8_t)(lt->tm_mon + 1),
                                                 (uint8_t)lt->tm_mday);

        // STM32가 CMD_UPDATE 처리 직후 곧바로 CMD_ACK(0xB0)를 보내므로 짧게 기다렸다 확인
        uint8_t ackStatus = 0;
        bool ackOk = ok && StmDisplayProtocol_ReadAck(fd, 200, &ackStatus);

        if (!ok)
        {
            // USB 뽑았다 꽂아도 기존 fd는 안 살아나므로 여기서 재연결 - 이번 주기는 그대로
            // 실패로 찍히고, 재연결에 성공했으면 다음 주기부터 다시 정상으로 돌아옴
            g_fd = StmDisplayProtocol_Reconnect(fd, kDevPath);
        }

        std::cout << "[" << CurrentTimeString() << "] [센서] 온도" << s.temp << " 습도" << s.humidity
                  << " 가스" << s.gasPpm << "ppm 연기" << s.smokePpm << "ppm"
                  << " 불꽃" << s.flameVal << "V"
                  << " -> 전송 " << (ok ? "성공" : "실패")
                  << " / 통신 " << (ackOk ? "양호 (ACK 수신)" : "불량 (ACK 없음)") << "\n";

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    StmDisplayProtocol_Close(g_fd.load());
    return 0;
}
