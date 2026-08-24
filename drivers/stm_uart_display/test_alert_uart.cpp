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
 *   콘솔 메뉴(숫자+엔터):
 *     1 : 위험(화재) 전환 화면(0x90) + 대피경로 전송(0xB1×4) + 화재없음 전송(0xB2, 개수0)
 *     2 : 평상시 화면 복귀(0xA0)
 *     3 : 화재 1곳 전송(0xB2) - (5,5) 반경4
 *     4 : 화재 2곳 전송(0xB2) - (5,5)반경4, (40,20)반경3
 *     5 : 화재 6곳(최대치) 전송(0xB2) - 버퍼 경계값 테스트
 *     6 : 화재 해제 전송(0xB2, 개수0) - "전부 진압됨" 상황
 *     7 : 대피경로만 재전송(0xB1×4) - 화재 상태는 그대로 두고 경로만 다시 그려지는지 확인
 *     8 : 화재 7곳 전송 시도 - 최대치(6) 초과라 SendEvacFires가 스스로 거부(false)하는지 확인용
 *   (경로는 화재랑 별개 패킷 - 화재 위치가 바뀌었다고 경로까지 매번 다시 보낼 필요 없음)
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

// 테스트용: Start ID 3(전광판 (36,11), EvacPlanner 기준 y,x) -> 출구 4곳 전체 경로.
// evac_routes.txt는 (y,x) 순서로 찍히지만, STM32 파서(main.c HandlePacket의 CMD_EVAC_PATH)는
// data[i*2]=x, data[i*2+1]=y로 읽으므로 여기 배열은 반드시 {x,y}로 "뒤집어서" 넣어야 함.
// (evac_routes.txt를 그대로 y,x 순서로 옮기면 x/y가 뒤바뀌어 엉뚱한 위치에 그려짐 - 실제로 겪은 버그)
// map.png 재수정 후 evac_server를 다시 돌려서 새로 갱신한 값 (evac_routes.txt 기준, x,y로 뒤집음)
// 배열 순서 = EvacExits 인덱스(routeIndex) 순서와 반드시 일치해야 함
struct EvacRoute { const uint8_t* xy; uint8_t count; };
static const uint8_t kRouteExit1[] = { 11,36, 11,34, 13,34, 13,48, 37,48, 37,58, 47,58, 47,59 };
static const uint8_t kRouteExit2[] = { 11,36, 11,34, 13,34, 13,47, 0,47 };
static const uint8_t kRouteExit3[] = { 11,36, 11,32, 13,32, 13,15, 59,15 };
static const uint8_t kRouteExit4[] = { 11,36, 11,32, 13,32, 13,15, 14,15, 14,12, 16,12, 16,1, 21,1, 21,0 };
static const EvacRoute kTestRoutes[] = {
    { kRouteExit1, sizeof(kRouteExit1) / 2 },
    { kRouteExit2, sizeof(kRouteExit2) / 2 },
    { kRouteExit3, sizeof(kRouteExit3) / 2 },
    { kRouteExit4, sizeof(kRouteExit4) / 2 },
};
static constexpr uint8_t kTestRouteCount = sizeof(kTestRoutes) / sizeof(kTestRoutes[0]);

// 5번 메뉴(화재 6곳: (5,5)r4,(15,10)r2,(25,40)r3,(40,20)r3,(50,50)r2,(8,45)r4)와 정확히 같은
// 조합으로 EvacPlanner(다익스트라 화재우회)를 다시 돌려서 나온 Start ID 3 결과.
// 이 조합에서는 전광판3이 4개 출구 전부 완전히 고립됨(도달 불가) - 화재가 심하면 경로 자체가
// 없어질 수 있다는 걸 그대로 재현하는 것도 테스트 의미가 있어서, 웨이포인트 0개로 그대로 둠
// (실제로 이 상태를 보내면 STM32 화면에서 해당 경로 라인이 전부 사라져야 정상).
static const EvacRoute kTestRoutesFire6[] = {
    { nullptr, 0 },
    { nullptr, 0 },
    { nullptr, 0 },
    { nullptr, 0 },
};

// 출구 4곳 경로를 전부(routeIndex 0~3) 순서대로 전송. 1회성으로 호출됨 -
// 주기적으로 계속 보내는 구조가 아니라, 위험 진입 시 한 번만 쏨
static void SendRoutes(const EvacRoute* routes, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++)
    {
        int fd = g_fd.load();
        bool ok = StmDisplayProtocol_SendEvacPath(fd, i, routes[i].xy, routes[i].count);
        std::cout << "[테스트] 대피경로 패킷(CMD 0xB1, 출구" << (int)(i + 1) << ", 웨이포인트 "
                  << (int)routes[i].count << "개) 전송 " << (ok ? "성공" : "실패") << "\n";
        if (!ok) { g_fd = StmDisplayProtocol_Reconnect(fd, kDevPath); }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));   // STM32 수신 링버퍼 여유 주기용
    }
}

static void SendAllEvacRoutes() { SendRoutes(kTestRoutes, kTestRouteCount); }

// 화재 목록 전송(CMD 0xB2). firesXYR = {x0,y0,r0,x1,y1,r1,...} 평탄화 배열, fireCount==0이면 화재 없음.
// 화재 위치가 바뀔 때만(1회성으로) 호출됨 - 경로(SendAllEvacRoutes)와 별개로 이것만 다시 보내면 됨
static void SendFires(const uint8_t* firesXYR, uint8_t fireCount)
{
    int fd = g_fd.load();
    bool ok = StmDisplayProtocol_SendEvacFires(fd, firesXYR, fireCount);
    std::cout << "[테스트] 화재 목록 패킷(CMD 0xB2, 화재 " << (int)fireCount << "곳) 전송 "
              << (ok ? "성공" : "실패") << "\n";
    if (!ok) { g_fd = StmDisplayProtocol_Reconnect(fd, kDevPath); }
}

static void PrintMenu()
{
    std::cout <<
        "\n[테스트 메뉴]\n"
        "  1 : 위험 전환(0x90) + 대피경로(0xB1x4) + 화재없음(0xB2, 0개)\n"
        "  2 : 평상시 복귀(0xA0)\n"
        "  3 : 화재 1곳 전송(0xB2) - (5,5) 반경4\n"
        "  4 : 화재 2곳 전송(0xB2) - (5,5)반경4, (40,20)반경3\n"
        "  5 : 화재 6곳(최대치) 전송(0xB2) + 그 화재 기준 실제 재계산 경로 전송(0xB1x4) - 전광판3은 이 조합에서 완전 고립돼서 경로 라인이 다 사라져야 정상\n"
        "  6 : 화재 해제(0xB2, 0개) - 전부 진압된 상황\n"
        "  7 : 대피경로만 재전송(0xB1x4) - 화재 상태는 그대로\n"
        "  8 : 화재 7곳 전송 시도 - 최대치(6) 초과, 거부(false)돼야 정상\n"
        "  9 : 가스 유출 위험 전환(0x90) - 전환화면 문구가 \"가스유출\"로 바뀌는지 확인용\n";
}

static void InputWorker()
{
    PrintMenu();
    std::string line;
    while (std::getline(std::cin, line))
    {
        int fd = g_fd.load();
        if (line == "1")
        {
            bool ok = StmDisplayProtocol_SendAlert(fd, STM_DISPLAY_DISASTER_FIRE, 0x01);
            std::cout << "[테스트] 위험 전환 패킷(CMD 0x90) 전송 " << (ok ? "성공" : "실패") << "\n";
            if (!ok) { g_fd = StmDisplayProtocol_Reconnect(fd, kDevPath); }
            SendAllEvacRoutes();
            SendFires(nullptr, 0);   // 화재 없음
        }
        else if (line == "2")
        {
            bool ok = StmDisplayProtocol_SendClear(fd);
            std::cout << "[테스트] 평상시 복귀 패킷(CMD 0xA0) 전송 " << (ok ? "성공" : "실패") << "\n";
            if (!ok) { g_fd = StmDisplayProtocol_Reconnect(fd, kDevPath); }
        }
        else if (line == "3")
        {
            // 화재 1곳: (5,5) 반경4
            static const uint8_t kTestFires1[] = { 5,5,4 };
            SendFires(kTestFires1, 1);
        }
        else if (line == "4")
        {
            // 화재 2곳이 새로 감지된 상황을 흉내냄: (5,5) 반경4, (40,20) 반경3
            static const uint8_t kTestFires2[] = { 5,5,4, 40,20,3 };
            SendFires(kTestFires2, 2);
        }
        else if (line == "5")
        {
            // STM_DISPLAY_EVAC_MAX_FIRES(6)와 정확히 같은 개수 - 버퍼가 딱 맞게 처리되는지 확인용.
            // 화재 패킷만 보내는 게 아니라, EvacPlanner로 이 화재 조합 기준 실제 재계산한 경로도
            // 같이 보냄(kTestRoutesFire6) - 전광판3은 이 조합에서 완전히 고립돼서 전부 웨이포인트 0개.
            static const uint8_t kTestFires6[] = {
                5,5,4,   15,10,2,  25,40,3,
                40,20,3, 50,50,2,  8,45,4
            };
            SendFires(kTestFires6, 6);
            SendRoutes(kTestRoutesFire6, kTestRouteCount);
        }
        else if (line == "6")
        {
            SendFires(nullptr, 0);   // 화재 전부 해제
        }
        else if (line == "7")
        {
            SendAllEvacRoutes();   // 화재는 안 건드리고 경로만 다시 보냄
        }
        else if (line == "8")
        {
            // 7곳(최대치 6 초과) - StmDisplayProtocol_SendEvacFires 안에서 count 체크로
            // 거부해야 정상(false 리턴, UART로 아예 안 나감). SendFires()가 "실패"라고 찍으면 정상.
            static const uint8_t kTestFires7[] = {
                5,5,4,   15,10,2,  25,40,3,
                40,20,3, 50,50,2,  8,45,4,  30,30,2
            };
            SendFires(kTestFires7, 7);
        }
        else if (line == "9")
        {
            // 가스 유출 시나리오 - 테두리/대피도 색상은 화재와 동일(RED), 전환화면 문구만 다름
            bool ok = StmDisplayProtocol_SendAlert(fd, STM_DISPLAY_DISASTER_GAS, 0x01);
            std::cout << "[테스트] 위험 전환 패킷(CMD 0x90, 가스) 전송 " << (ok ? "성공" : "실패") << "\n";
            if (!ok) { g_fd = StmDisplayProtocol_Reconnect(fd, kDevPath); }
            SendAllEvacRoutes();
            SendFires(nullptr, 0);   // 화재 없음
        }
        else if (!line.empty())
        {
            std::cout << "[테스트] 1~9 중 하나만 입력 가능\n";
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
