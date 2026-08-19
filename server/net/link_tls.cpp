#include "link.h"
#include "../tls/TlsServer.h"
#include <memory>
#include <thread>
#include <iostream>

// TLS 버전. Link 인터페이스를 TlsServer 위에 얇게 얹는다.
// ENABLE_TLS 켜면 link_plain.cpp 대신 이 파일이 빌드에 들어감 (CMakeLists.txt 참고)
class TlsLink : public Link {
public:
    ~TlsLink() override { stop(); }

    bool start(int port) override {
        // TlsServer 생성자가 인증서 로드 실패 시 exit()으로 프로세스를 종료시킬 수 있음(기존 동작).
        // PlainLink처럼 false를 반환하고 계속 진행하는 경로는 없다 — 알려진 차이점
        server_ = std::make_unique<TlsServer>(port);
        runThread_ = std::thread([this] { server_->run(); });
        std::cout << "[링크] TLS 대기 시작 (포트 " << port << ")\n";
        return true;
    }

    // 큐에 넣고 즉시 리턴 (TlsServer::enqueueTxData가 이미 그렇게 동작)
    void send(const std::string& line) override {
        if (server_) server_->enqueueTxData(line);
    }

    // 한 줄 꺼냄(블로킹). stop() 되면 false
    bool recvLine(std::string& out) override {
        return server_ && server_->waitAndPopRxLine(out);
    }

    bool connected() const override {
        return server_ && server_->isConnected();
    }

    void stop() override {
        if (server_) server_->stop();
        if (runThread_.joinable()) runThread_.join();
    }

private:
    std::unique_ptr<TlsServer> server_;
    std::thread runThread_;
};

// CMake가 link_plain.cpp / link_tls.cpp 중 하나만 빌드에 넣음
Link* CreateLink() { return new TlsLink(); }
