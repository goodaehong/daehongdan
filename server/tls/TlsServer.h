#ifndef TLS_SERVER_H
#define TLS_SERVER_H

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "json.hpp" // nlohmann/json 라이브러리 포함

class TlsServer {
public:
    explicit TlsServer(int port = 9999);
    ~TlsServer();

    // 서버 소켓 바인딩 및 클라이언트 연결 대기 루프 수행
    void run();

    // 외부 스레드(영상 처리, 센서 드라이버 등)에서 송신 큐에 암호화 전송할 데이터를 삽입
    void enqueueTxData(const std::string& jsonData);

    // 하드웨어 제어 스레드가 파싱된 제어 명령을 꺼내갈 때 사용 (블로킹 대기, 서버 종료 시 false 반환)
    bool waitAndPopRxCommand(nlohmann::json& outCommand);

private:
    void initOpenSSL();
    void cleanupOpenSSL();
    SSL_CTX* createContext();
    void configureContext(SSL_CTX* ctx);

    // 내부 송신 전용 스레드 함수
    void txThreadLoop();

    int serverPort_;
    int serverFd_;

    SSL_CTX* ctx_;
    SSL* ssl_;

    // ssl_ 포인터(생성/해제) 및 SSL_read/SSL_write 보호
    std::mutex sslMutex_;

    // 송신 데이터 큐 및 동기화 제어 변수
    std::queue<std::string> txQueue_;
    std::mutex txMutex_;
    std::condition_variable txCv_;
    std::thread txThread_;

    // 수신된 제어 명령 큐 및 동기화 제어 변수 (하드웨어 제어 스레드가 소비)
    std::queue<nlohmann::json> rxQueue_;
    std::mutex rxMutex_;
    std::condition_variable rxCv_;

    std::atomic<bool> isRunning_;
};

#endif // TLS_SERVER_H
