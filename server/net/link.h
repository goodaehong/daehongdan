#pragma once

// Qt 연결. 평문/TLS를 같은 얼굴로 쓰기 위한 인터페이스
// 전송은 큐에 넣고 즉시 리턴 — Qt가 느려도 감지·센서 스레드가 안 멈추게

#include <string>

class Link {
public:
    virtual ~Link() = default;

    virtual bool start(int port) = 0;                // listen + 내부 스레드 기동
    virtual void send(const std::string& line) = 0;  // 큐에 넣고 리턴. "\n"은 내부에서
    virtual bool recvLine(std::string& out) = 0;     // 한 줄 꺼냄(블로킹). 종료 시 false
    virtual bool connected() const = 0;
    virtual void stop() = 0;
};

// 빌드 옵션(ENABLE_TLS)에 따라 평문/TLS 구현 반환
Link* CreateLink();

// 소켓 fd 노출 금지 — TLS는 recv() 대신 SSL_read()라 fd 꺼내 쓰면 교체 불가
// 이 모듈은 스레드 규칙 예외 (블로킹 소켓 I/O라 내부 송수신 스레드 필요)