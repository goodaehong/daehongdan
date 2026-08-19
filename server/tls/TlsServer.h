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

class TlsServer {
public:
    explicit TlsServer(int port = 9999);
    ~TlsServer();

    // 서버 소켓 바인딩 및 클라이언트 연결 대기 루프 수행 (블로킹 — 호출자가 스레드로 돌려야 함)
    void run();

    // 실행 중지: 송신 스레드 정리 + 세션 종료 + accept 대기 해제(serverFd 닫음)
    void stop();

    // 외부 스레드(영상 처리, 센서 드라이버 등)에서 송신 큐에 암호화 전송할 데이터를 삽입
    void enqueueTxData(const std::string& jsonData);

    // 수신 라인 하나를 꺼냄(블로킹). 타입 구분 없이 \n 단위 원문 그대로 — 라우팅은 상위(qt_link)가 담당
    // 서버 종료 시 false 반환
    bool waitAndPopRxLine(std::string& outLine);

    bool isConnected() const { return connected_; }

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

    // 수신 라인 큐 및 동기화 제어 변수 (Link::recvLine 대응 — 상위가 타입별로 소비)
    std::queue<std::string> rxQueue_;
    std::mutex rxMutex_;
    std::condition_variable rxCv_;

    std::atomic<bool> isRunning_;
    std::atomic<bool> connected_{false};

    static constexpr size_t MAX_LINE = 8 * 1024 * 1024;   // 평면도 base64 등 대용량 메시지 여유분
};

#endif // TLS_SERVER_H
