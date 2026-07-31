#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <iostream>
#include <fstream> // ★ 추가: 파일 읽기용 (DHT22)
#include <cmath> // ★ 추가: pow 필요
#include <chrono>
#include <sstream>
#include <iomanip>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include "FireDetectionRuntime.h"
#include "../drivers/stm_uart_display/stm_display_protocol.h"
#include <random>
#include <ctime>
#include <atomic>
#include <cstdlib>

// 4채널 프레임 공유 저장소. 채널별 mutex로 워커/메인 간 경합 방지
struct FrameStore {
    cv::Mat frames[4];
    std::mutex mtx[4];
};

// 채널별 최신 감지 상태. 워커가 갱신, 판단(센서 스레드)이 읽음
struct DetectionState {
    std::atomic<bool> fire{false};
    std::atomic<bool> smoke{false};
};
DetectionState detState[4];

// 감지 JSON을 TCP로 내보내는 공용 sender. connect(실서비스)/listen(테스트) 둘 다 지원.
class Sender {
public:
    // ── 실서비스용: 광렬 TLS 서버로 접속 (원래 있던 함수, 그대로 유지) ──
    bool connect(const std::string& host, int port) {
        std::lock_guard<std::mutex> lock(mtx_);
        mode_ = Mode::Client;                 // ★ 이 한 줄만 추가 (모드 표시)
        host_ = host;
        port_ = port;
        return connectLocked();
    }

    // ★★★ 여기부터 listen() 함수 통째로 새로 추가 ★★★
    // ── 테스트용: Qt(ServerLink)가 직접 접속하도록 서버가 listen ──
    bool listen(int port) {
        std::lock_guard<std::mutex> lock(mtx_);
        mode_ = Mode::Server;
        listenfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd_ < 0) return false;
        int opt = 1;
        ::setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        sockaddr_in addr{};
        addr.sin_family = AF_INET; addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (::bind(listenfd_, (sockaddr*)&addr, sizeof(addr)) < 0) { ::close(listenfd_); listenfd_ = -1; return false; }
        if (::listen(listenfd_, 1) < 0) { ::close(listenfd_); listenfd_ = -1; return false; }
        int fl = ::fcntl(listenfd_, F_GETFL, 0);
        ::fcntl(listenfd_, F_SETFL, fl | O_NONBLOCK);
        return true;
    }
    // ★★★ listen() 끝 ★★★

    // 수신 스레드가 현재 연결된 Qt 소켓을 알아낼 수 있게 공개        
    int clientFd() {
        std::lock_guard<std::mutex> lock(mtx_);
        return clientfd_;
    }                                                               
    
    // ── send(): 원래는 client 전용이었는데, 모드 분기하도록 내용 교체 ──
    void send(const std::string& line) {
        std::lock_guard<std::mutex> lock(mtx_);
        int fd;                                       // ★ 아래 if/else 블록 전체가 새 내용
        if (mode_ == Mode::Server) {                  // ★ 테스트(listen) 모드
            int newfd = ::accept(listenfd_, nullptr, nullptr);   // ★ 새 접속 있으면 받기
            if (newfd >= 0) {                                    // ★ 새 클라가 붙었으면
                if (clientfd_ >= 0) ::close(clientfd_);          // ★ 이전(죽은) 연결 닫고
                clientfd_ = newfd;                               // ★ 새 걸로 교체
            }
            if (clientfd_ < 0) return;                           // ★ 아직 아무도 안 붙음 → 버림
            fd = clientfd_;
        } else {                                      // ★ 실서비스(connect) 모드 = 원래 로직
            if (sockfd_ < 0 && !connectLocked()) return;
            fd = sockfd_;
        }
        std::string buf = line + "\n";                // (여기부터는 원래와 거의 같음)
        ssize_t n = ::send(fd, buf.data(), buf.size(), MSG_NOSIGNAL);
        if (n <= 0) {
            ::close(fd);
            if (mode_ == Mode::Server) clientfd_ = -1; else sockfd_ = -1;   // ★ 모드별로 닫기
        }
    }

    ~Sender() {
        if (sockfd_ >= 0) ::close(sockfd_);
        if (clientfd_ >= 0) ::close(clientfd_);       // ★ 추가
        if (listenfd_ >= 0) ::close(listenfd_);       // ★ 추가
    }

private:
    // ── connectLocked(): 원래 그대로, 하나도 안 바뀜 ──
    bool connectLocked() {
        sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
            ::close(sockfd_); sockfd_ = -1; return false;
        }
        if (::connect(sockfd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            ::close(sockfd_); sockfd_ = -1; return false;
        }
        return true;
    }

    enum class Mode { Client, Server };   // ★ 추가 (모드 종류)
    Mode mode_ = Mode::Client;            // ★ 추가 (현재 모드)
    int sockfd_ = -1;                     //   원래 있던 것
    int listenfd_ = -1;                   // ★ 추가 (listen 소켓)
    int clientfd_ = -1;                   // ★ 추가 (accept된 Qt 소켓)
    std::mutex mtx_;                      //   원래 있던 것
    std::string host_;                    //   원래 있던 것
    int port_ = 0;                        //   원래 있던 것
};

// ── JSON 필드 추출 헬퍼 (라이브러리 없이 문자열 검색으로) ──          // <- 처음
std::string jsonStr(const std::string& j, const std::string& key) {
    std::string pat = "\"" + key + "\":\"";
    size_t s = j.find(pat);
    if (s == std::string::npos) return "";
    s += pat.size();
    size_t e = j.find('"', s);
    return (e == std::string::npos) ? "" : j.substr(s, e - s);
}
int jsonInt(const std::string& j, const std::string& key, int def) {
    std::string pat = "\"" + key + "\":";
    size_t s = j.find(pat);
    if (s == std::string::npos) return def;
    return std::atoi(j.c_str() + s + pat.size());
}

// ── 액추에이터 현재 상태 (명세서 actuator_status 값 그대로) ──
struct ActuatorState {
    std::atomic<int> fan{0};     // 0=OFF, 1~3=약/중/강
    std::atomic<int> valve{1};   // 1=열림(평상시), 0=닫힘
    std::atomic<int> siren{0};   // 0=OFF, 1=ON
};
ActuatorState actState;
std::mutex uartMtx;   // 자동(센서 스레드)·수동(수신 스레드) 명령 직렬화
std::atomic<bool> g_warningAck{false};   // 경고 확인(warning_ack) 수신 플래그. 수신 스레드가 set, 센서 스레드가 read/clear

// ── 명령 실행부. 수동·자동 모두 여기로 수렴. 나중에 STM UART도 이 안에만 추가 ──
void executeCommand(const std::string& target, const std::string& action,
                    int value, const std::string& src, Sender& sender) {
    std::lock_guard<std::mutex> lock(uartMtx);   // 동시 실행 방지 (한 명령씩)

    if      (target == "fan") {                                      // 
        if      (action == "off")  actState.fan = 0;
        else if (action == "low")  actState.fan = 1;
        else if (action == "mid")  actState.fan = 2;
        else if (action == "high") actState.fan = 3;
        else                       actState.fan = value;   // 자동 대응 등 value 방식 폴백
    }                                                               
    else if (target == "valve") actState.valve = (action == "open") ? 1 : 0;
    else if (target == "siren") actState.siren = (action == "on")   ? 1 : 0;
    else return;   // 모르는 대상은 무시

    std::cout << "[제어][" << src << "] " << target << " action=" << action   
              << " value=" << value << "\n";
    // TODO(STM 연결 후): 여기서 UART 패킷 전송 + ACK 수신

    // 실행 결과를 Qt에 보고 → 화면의 팬/밸브/사이렌 표시 갱신
    std::ostringstream oss;
    oss << "{\"type\":\"actuator_status\",\"fan\":" << actState.fan
        << ",\"valve\":" << actState.valve
        << ",\"siren\":" << actState.siren << "}";
    sender.send(oss.str());
}

// ── 수신한 한 줄 처리: control이면 실행 + ack 응답 ──
void handleControl(const std::string& line, Sender& sender) {
    if (line.find("\"type\":\"control\"") == std::string::npos) return;

    std::string cmdId  = jsonStr(line, "cmdId");
    std::string zone   = jsonStr(line, "zone");
    std::string target = jsonStr(line, "target");
    std::string action = jsonStr(line, "action");
    int value          = jsonInt(line, "value", 0);

    executeCommand(target, action, value, "수동", sender);

    // 명세서 control_ack 규격: cmdId 반사, 지금은 무조건 ok (STM 없으니 실패할 게 없음)
    std::ostringstream oss;
    oss << "{\"type\":\"control_ack\",\"cmdId\":\"" << cmdId
        << "\",\"zone\":\"" << zone << "\",\"target\":\"" << target
        << "\",\"result\":\"ok\",\"reason\":null,\"ts\":" << std::time(nullptr) << "}";
    sender.send(oss.str());
}

// ── 수신 스레드: Qt→서버 방향 개통. 나중에 광렬님 waitAndPopRxCommand로 교체되는 부분 ──
void recvWorker(Sender& sender) {
    std::string buf;
    char tmp[512];
    while (true) {
        int fd = sender.clientFd();
        if (fd < 0) {   // 아직 Qt 안 붙음
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), MSG_DONTWAIT);   // 논블로킹 읽기
        if (n > 0) {
            buf.append(tmp, n);
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {  // \n 단위로 자르기
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                if (line.find("\"type\":\"warning_ack\"") != std::string::npos)  
                    g_warningAck = true;   // 관리자 인지 → 센서 스레드가 타이머 취소
                else                 
                    handleControl(line, sender);
            }
        } else if (n == 0) {   // 연결 끊김
            buf.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else {               // 데이터 없음(EAGAIN) 또는 일시 에러
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}                                                                    


// 판단 매트릭스: 카메라 감지 + 센서값 융합 → 종합 상태 + 원인          // <- 처음 (기존 judgeState 통째 교체)
struct Judgement {
    std::string state;   // "safe"/"warning"/"danger" → Qt로 전송
    std::string cause;   // 대응 선택용 
};

Judgement judgeState(bool camFire, bool camSmoke, float gasPpm, float smokePpm) {
    bool gasHigh   = gasPpm   > 200.0f;   // 임계값: 유나님 보정 후 확정
    bool smokeHigh = smokePpm > 150.0f;

    if (camFire  && gasHigh)   return {"danger",  "fire_gas"};    // 우선순위 1
    if (camFire)               return {"danger",  "flame"};       // 2
    if (camSmoke && smokeHigh) return {"danger",  "smoke_fire"};  // 3
    if (gasHigh)               return {"danger",  "gas"};         // 4
    if (camSmoke)              return {"warning", "smoke_watch"}; // 5
    return {"safe", ""};
}                                                                    // <- 끝

// ── [추가] DHT22 드라이버에서 온습도 읽어오기 ──
bool readDHT22(float& out_temp, float& out_hum) {
    std::ifstream tempFile("/sys/devices/platform/dht22/temp_value");
    std::ifstream humFile("/sys/devices/platform/dht22/humid_value");

    if (tempFile.is_open() && humFile.is_open()) {
        tempFile >> out_temp;
        humFile >> out_hum;
        return true;
    }
    return false; // 읽기 실패
}
// ──────────────────────────────────────────────

// ── [추가] ADS1115 raw -> ppm 환산 ──
constexpr float ADS1115_LSB = 6.144f / 32768.0f; // V/count (PGA ±6.144V, 16bit)
constexpr float SENSOR_VCC = 5.0f; // MQ 모듈 공급전압
constexpr float RL_KOHM = 5.0f; // 모듈 로드저항 (보드 실측 필요, 보통 5kΩ)

// 클린에어 보장값 - 반드시 클린에어에서 측정 후 갱신할 것
constexpr float MQ2_RO_KOHM = 10.0f; // TODO: 실측값으로 교체
constexpr float MQ9_RO_KOHM = 10.0f; // TODO: 실측값으로 교체

float rawToRsKohm(int raw) {
    float voltage = raw * ADS1115_LSB;
    if (voltage < 0.05f) voltage = 0.05f; // 0 나눗셈 방지
    return RL_KOHM * (SENSOR_VCC - voltage) / voltage;
}

// MQ-2 연기(smoke) 커브 (잠정치, 데이터시트 재검증 필요)
float mq2ToSmokePpm(int raw) {
    float ratio = rawToRsKohm(raw) / MQ2_RO_KOHM;
    return 3616.1f * std::pow(ratio, -2.629f);
}

// MQ-9 가스(CO) 커브 (잠정치, 데이터시트 재검증 필요)
float mq9ToGasPpm(int raw) {
    float ratio = rawToRsKohm(raw) / MQ9_RO_KOHM;
    return 605.0f * std::pow(ratio, -2.336f);
}

// ── [추가] ADS1115 드라이버에서 raw 값 읽기 ── 
bool readADS1115(int& raw_mq9, int& raw_mq2) {
    // ⚠️ 경로 확인 필요: i2c 드라이버라 platform/이 아니라
    //    /sys/bus/i2c/devices/1-0048/ 쪽일 가능성 높음
    //    확인: find /sys -name "mq9_value" 2>/dev/null
    std::ifstream mq9f("/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/mq9_value");
    std::ifstream mq2f("/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/mq2_value");

    if (mq9f.is_open() && mq2f.is_open()) {
        mq9f >> raw_mq9;
        mq2f >> raw_mq2;
        return true;
    }
    return false;
}

// mock 센서 스레드. 부품 오면 값 생성부만 실제 드라이버 읽기로 교체
void sensorWorker(Sender& sender, int stmFd) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    int tick = 0;
    std::string prevState = "safe"; 
    std::string prevCause = "";
    float prevTemp = 26.0f; // [추가] 초기값은 기존 mock 기준값
    float prevHumidity = 45.0f; // [추가]
    const int WARN_TIMEOUT = 10;   // 경고 무응답 자동 전환까지 (초, N)   
    int  warnStartTick = -1;       // warning 진입 tick (-1 = 타이머 비활성)
    bool forcedDanger  = false;    // 무응답으로 강제 위험 전환된 상태     

    while (true) {
        // ── 온습도: 실제 DHT22 드라이버 값으로 교체 ──
        float temp, humidity;
        if (!readDHT22(temp, humidity)) {
            std::cerr << "[DHT22] 읽기 실패, 이전 값 유지\n";
            temp = prevTemp;
            humidity = prevHumidity;
        } else {
            prevTemp = temp;
            prevHumidity = humidity;
        }

        // // 평상시 기준값 + 흔들림
        // float temp     = 26.0f + jitter(rng) * 2.0f;
        // float humidity = 45.0f + jitter(rng) * 5.0f;

        // // 가스/연기는 아직! 기존 mock 그대로 유지 (state/danger 로직 테스트용)
        // float gasPpm   = 45.0f + jitter(rng) * 10.0f;
        // float smokePpm = 8.0f  + jitter(rng) * 3.0f;

        static float prevGasPpm = 45.0f, prevSmokePpm = 8.0f; // 실패 시 폴백용 초기값
        float gasPpm, smokePpm;
        int raw_mq9, raw_mq2;

        if (!readADS1115(raw_mq9, raw_mq2)) {
            std::cerr << "[ADS1115] 읽기 실패ㅡ 이전 값 유지\n";
            gasPpm = prevGasPpm;
            smokePpm = prevSmokePpm;
        } else {
            gasPpm = mq9ToGasPpm(raw_mq9);
            smokePpm = mq2ToSmokePpm(raw_mq2);
            prevGasPpm = gasPpm;
            prevSmokePpm = smokePpm;
        }

        // // 데모 스파이크: 60초 주기 중 45~60초 구간은 가스 급상승 (Qt 경고 UI 테스트용)
        // if (tick % 60 >= 45) {
        //     gasPpm   = 250.0f + jitter(rng) * 30.0f;
        //     smokePpm = 180.0f + jitter(rng) * 20.0f;
        // }

        // ── [카메라 상태 종합] 4채널 중 하나라도 감지면 true ──
        bool camFire = false, camSmoke = false;
        for (int i = 0; i < 4; i++) {
            if (detState[i].fire)  camFire  = true;
            if (detState[i].smoke) camSmoke = true;
        }

        Judgement j = judgeState(camFire, camSmoke, gasPpm, smokePpm);     

        // ── 경고 무응답 타이머: warning 지속 중 관리자 미확인 시 위험 강제 전환 ──  
        int warnRemain = -1;   // -1 = JSON에 미포함 (warning 아닐 때)
        if (j.state == "warning") {
            if (warnStartTick < 0) {        // warning 진입 순간
                warnStartTick = tick;
                g_warningAck  = false;      // 새 경고 시작 → 이전 ack 무효화
                forcedDanger  = false;
            }
            if (g_warningAck.load()) {
                warnRemain = 0;             // 관리자 확인함 → 카운트다운 종료(전환 안 함)
            } else {
                warnRemain = WARN_TIMEOUT - (tick - warnStartTick);
                if (warnRemain <= 0) {      // 무응답 → 강제 위험 전환
                    warnRemain   = 0;
                    forcedDanger = true;
                }
            }
        } else {
            warnStartTick = -1;             // warning 벗어남 → 타이머 리셋
            if (j.state == "safe") forcedDanger = false;   // 안전 복귀 시 강제상태 해제
        }

        // 강제 전환: 자연 판단이 warning이어도 danger로 승격 (미확인 연기 → 화재계열 대응=팬 차단)
        if (forcedDanger && j.state == "warning") {
            j.state = "danger";
            j.cause = "smoke_fire";
        }                                                                         

        // 엣지 트리거: 위험 "진입" 또는 위험 중 "원인 변경" 순간에만 발사
        // (가스로 팬 최대 배출 중 → 불 붙음(fire_gas) → 팬 차단으로 뒤집어야 함)
        if (j.state == "danger" && (prevState != "danger" || j.cause != prevCause)) {
            std::string src = "자동:" + j.cause;
            if (j.cause == "gas") {                    // 4행: 가스만 → 팬 최대 (배출)
                executeCommand("siren", "on",    0, src, sender);
                executeCommand("valve", "close", 0, src, sender);
                executeCommand("fan",   "high",  0, src, sender);
                StmDisplay_SendAlert(stmFd, STM_DISPLAY_DISASTER_GAS, 0x01);   // 전광판: 가스유출 화면
            }
            else {                                     // 1~3행 화재 계열 → 팬 차단 (산소 차단)
                executeCommand("siren", "on",    0, src, sender);
                executeCommand("valve", "close", 0, src, sender);
                executeCommand("fan",   "off",   0, src, sender);
                StmDisplay_SendAlert(stmFd, STM_DISPLAY_DISASTER_FIRE, 0x01);  // 전광판: 화재발생 화면
            }
        }
        else if (prevState == "danger" && j.state == "safe") {
            std::string src = "자동:해제";
            executeCommand("siren", "off",  0, src, sender);
            executeCommand("valve", "open", 0, src, sender);   // TODO: 자동 재개방 여부 팀 결정
            executeCommand("fan",   "low",  0, src, sender);   // 평상시 약 가동 복귀
        }
        prevState = j.state;
        prevCause = j.cause;

        // ── STM32 전광판 평상시 갱신 (CMD 0x80) ──
        {
            uint8_t face, gasColor;
            if      (j.state == "danger")  { face = STM_DISPLAY_STATE_DANGER;  gasColor = STM_DISPLAY_STATE_DANGER; }
            else if (j.state == "warning") { face = STM_DISPLAY_STATE_WARNING; gasColor = STM_DISPLAY_STATE_WARNING; }
            else                            { face = STM_DISPLAY_STATE_SAFE;    gasColor = STM_DISPLAY_STATE_SAFE; }

            std::time_t nowT = std::time(nullptr);
            std::tm* lt = std::localtime(&nowT);

            if (!StmDisplay_SendUpdate(stmFd, face, gasColor, (uint16_t)gasPpm,
                                       (uint8_t)temp, (uint8_t)humidity,
                                       (uint8_t)lt->tm_hour, (uint8_t)lt->tm_min,
                                       (uint8_t)(lt->tm_year % 100), (uint8_t)(lt->tm_mon + 1), (uint8_t)lt->tm_mday))
            {
                std::cerr << "[STM 전광판] 갱신 전송 실패\n";
            }
        }

        // ── [JSON 조립] 명세서 "센서 정보" 스키마 그대로 ──
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        oss << "{\"type\":\"sensor\",\"zone\":\"A\""
            << ",\"ts\":" << std::time(nullptr)
            << ",\"temp\":" << temp
            << ",\"humidity\":" << humidity
            << ",\"gasPpm\":" << gasPpm
            << ",\"smokePpm\":" << smokePpm
            << ",\"state\":\"" << j.state << "\""
            << ",\"cause\":\"" << j.cause << "\"";                  
        if (j.state == "warning") oss << ",\"warnRemain\":" << warnRemain;
        oss << "}";                                                 
        sender.send(oss.str());

        tick++;
        if (tick % 5 == 0) {                                       
            // 액추에이터 상태 주기 보고: Qt가 새로 접속해도 화면 동기화되게
            std::ostringstream st;
            st << "{\"type\":\"actuator_status\",\"fan\":" << actState.fan
               << ",\"valve\":" << actState.valve
               << ",\"siren\":" << actState.siren << "}";
            sender.send(st.str());
        }  
        std::this_thread::sleep_for(std::chrono::seconds(1));  // 전송 주기 1초
    }
}

// ── [테스트용] 콘솔에 "fire"/"gas" 입력하면 STM32로 CMD 0x90(위험 대피 전환) 수동 전송.
//    실제 센서 없이도 STM32 쪽 화재/가스 전환 화면 동작을 확인할 수 있게 하기 위한 용도 ──
void stmAlertTestWorker(int stmFd) {
    std::cout << "[테스트] 콘솔에 'fire' 또는 'gas' 입력 후 엔터 -> STM32 전환 화면 전송\n";
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "fire") {
            bool ok = StmDisplay_SendAlert(stmFd, STM_DISPLAY_DISASTER_FIRE, 0x01);
            std::cout << "[테스트] 화재발생 전환 패킷 전송 " << (ok ? "성공" : "실패") << "\n";
        } else if (line == "gas") {
            bool ok = StmDisplay_SendAlert(stmFd, STM_DISPLAY_DISASTER_GAS, 0x01);
            std::cout << "[테스트] 가스유출 전환 패킷 전송 " << (ok ? "성공" : "실패") << "\n";
        } else if (!line.empty()) {
            std::cout << "[테스트] 'fire' 또는 'gas'만 입력 가능\n";
        }
    }
}

// 채널 1개 담당. RTSP 연결 → 프레임 읽기 → 감지 → JSON 전송. 끊기면 재연결
void worker(int ch, FrameStore& store, Sender& sender) {
    std::string url = "rtsp://localhost:8554/cam" + std::to_string(ch + 1);

    FireDetectionRuntime runtime;   // 복사 금지 타입 → 채널당 지역변수 1개
    std::uint64_t frameId = 0;
    bool wasShowingBoxes = false;  

    while (true) {
        cv::VideoCapture cap;
        cap.open(url, cv::CAP_FFMPEG);
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);   // ★ 추가 — 버퍼 1프레임 = 항상 최신 처리

        if (!cap.isOpened()) {
            std::cerr << "[cam" << ch + 1 << "] 연결 실패, 3초 후 재시도\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        std::cout << "[cam" << ch + 1 << "] 연결 성공\n";
        runtime.resetStream();   // 재연결이면 이전 추적 상태 폐기

        cv::Mat frame;
        while (true) {
            if (!cap.read(frame) || frame.empty()) {
                std::cerr << "[cam" << ch + 1 << "] 프레임 읽기 실패, 재연결\n";
                break;
            }
            {
                std::lock_guard<std::mutex> lock(store.mtx[ch]);
                store.frames[ch] = frame.clone();
            }

            if (frameId % 1 == 0) {
                runtime.submitFrame(frame, frameId);
            }
            frameId++;
            FireRuntimeSnapshot snap = runtime.poll();

            if (snap.boxIsFresh) {
                std::cout << "[cam" << ch + 1 << "] boxIsFresh! boxes="
                          << snap.detection.boxes.size()
                          << " alarm=" << snap.alarm.alarmActive << "\n";   // ★ 임시 디버그

                bool hasFire = false, hasSmoke = false;                      
                for (const auto& b : snap.detection.boxes) {
                    if (b.type == DetectionType::FIRE) hasFire = true;
                    else                               hasSmoke = true;
                }
                detState[ch].fire  = hasFire && snap.alarm.alarmActive;
                detState[ch].smoke = hasSmoke;                                
                          
                // 계약① JSON 조립 (박스 0개여도 전송 → Qt가 오버레이 지움)
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2);
                oss << "{\"type\":\"detection\""
                    << ",\"channel\":" << (ch + 1)
                    << ",\"frameId\":" << snap.resultFrameId
                    << ",\"srcW\":" << frame.cols
                    << ",\"srcH\":" << frame.rows
                    << ",\"alarm\":" << (snap.alarm.alarmActive ? "true" : "false")
                    << ",\"boxes\":[";

                for (size_t i = 0; i < snap.detection.boxes.size(); ++i) {
                    const auto& b = snap.detection.boxes[i];
                    if (i > 0) oss << ",";
                    oss << "{\"x\":" << b.box.x
                        << ",\"y\":" << b.box.y
                        << ",\"w\":" << b.box.width
                        << ",\"h\":" << b.box.height
                        << ",\"cls\":\"" << (b.type == DetectionType::FIRE ? "FIRE" : "SMOKE") << "\""
                        << ",\"score\":" << b.score
                        << "}";
                }
                oss << "]}";

                sender.send(oss.str());
                wasShowingBoxes = true;
            }
            else if (wasShowingBoxes && !snap.alarm.alarmActive) {                 
                std::ostringstream oss;
                oss << "{\"type\":\"detection\",\"channel\":" << (ch + 1)
                    << ",\"srcW\":" << frame.cols << ",\"srcH\":" << frame.rows
                    << ",\"alarm\":false,\"boxes\":[]}";
                sender.send(oss.str());
                wasShowingBoxes = false;               
                detState[ch].fire  = false;                               
                detState[ch].smoke = false; 
            }
        }

        cap.release();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

int main() {
    setenv("OPENCV_FFMPEG_CAPTURE_OPTIONS",
           "rtsp_transport;tcp|fflags;nobuffer|flags;low_delay", 1);   // ★ 저지연 옵션
    cv::setNumThreads(1);   // ★ OpenCV 채널당 1스레드 = 멀티채널 최적화 핵심
    Sender sender;
    sender.listen(9999);                    // [지금 테스트] Qt 직접 접속
    //sender.connect("127.0.0.1", 9999);   // mock 테스트 포트. 나중에 광렬 TLS 서버 포트로 교체

    FrameStore store;
    std::thread threads[4];

    int stmFd = StmDisplay_Open("/dev/serial0");   // STM32 display_board UART
    if (stmFd < 0) {
        std::cerr << "[STM 전광판] UART 열기 실패 - 전광판 업데이트 없이 계속 진행\n";
    }

    std::thread sensorThread(sensorWorker, std::ref(sender), stmFd);
    sensorThread.detach();

    std::thread alertTestThread(stmAlertTestWorker, stmFd);
    alertTestThread.detach();

    std::thread recvThread(recvWorker, std::ref(sender));
    recvThread.detach();

    for (int i = 0; i < 4; i++) {
        threads[i] = std::thread(worker, i, std::ref(store), std::ref(sender));
    }

    for (int i = 0; i < 4; i++) {
        threads[i].join();
    }

    return 0;
}