#include "TlsServer.h"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <cerrno>

using json = nlohmann::json;

TlsServer::TlsServer(int port) : serverPort_(port), ctx_(nullptr), ssl_(nullptr), isRunning_(false), serverFd_(-1) {
    initOpenSSL();
    ctx_ = createContext();
    configureContext(ctx_);
}

TlsServer::~TlsServer() {
    isRunning_ = false;
    txCv_.notify_all(); // 대기 중인 TX 스레드 종료 신호 전달
    rxCv_.notify_all(); // 대기 중인 RX 소비자(하드웨어 제어 스레드) 종료 신호 전달

    if (txThread_.joinable()) {
        txThread_.join();
    }
    if (ssl_) {
        SSL_free(ssl_);
    }
    if (serverFd_ >= 0) {
        close(serverFd_);
    }
    cleanupOpenSSL();
}

void TlsServer::initOpenSSL() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

void TlsServer::cleanupOpenSSL() {
    EVP_cleanup();
}

SSL_CTX* TlsServer::createContext() {
    const SSL_METHOD* method = TLS_server_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        std::cerr << "[TLS] 컨텍스트 생성 실패" << std::endl;
        exit(EXIT_FAILURE);
    }
    return ctx;
}

void TlsServer::configureContext(SSL_CTX* ctx) {
    // 상대 경로 기반 테스트용 x509 인증서 및 개인키 로드
    if (SSL_CTX_use_certificate_file(ctx, "server_cert.pem", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, "server_private.pem", SSL_FILETYPE_PEM) <= 0 ) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
}

void TlsServer::enqueueTxData(const std::string& jsonData) {
    {
        std::lock_guard<std::mutex> lock(txMutex_);
        txQueue_.push(jsonData + "\n");
    }
    txCv_.notify_one();
}

void TlsServer::txThreadLoop() {
    while (isRunning_) {
        std::string dataToSend;
        {
            std::unique_lock<std::mutex> lock(txMutex_);
            txCv_.wait(lock, [this] { return !txQueue_.empty() || !isRunning_; });

            if (!isRunning_ && txQueue_.empty()) {
                break;
            }

            dataToSend = txQueue_.front();
            txQueue_.pop();
        }

        std::lock_guard<std::mutex> sslLock(sslMutex_);
        if (ssl_) {
            int bytes = SSL_write(ssl_, dataToSend.c_str(), dataToSend.length());
            if (bytes <= 0) {
                std::cerr << "[TLS] 데이터 송신 실패" << std::endl;
            }
        }
    }
}

bool TlsServer::waitAndPopRxCommand(json& outCommand) {
    std::unique_lock<std::mutex> lock(rxMutex_);
    rxCv_.wait(lock, [this] { return !rxQueue_.empty() || !isRunning_; });

    if (rxQueue_.empty()) {
        return false; // 서버 종료로 인해 깨어남
    }

    outCommand = rxQueue_.front();
    rxQueue_.pop();
    return true;
}

void TlsServer::run() {
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        std::cerr << "[TLS] 소켓 생성 실패" << std::endl;
        return;
    }

    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(serverPort_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverFd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[TLS] 소켓 바인딩 실패" << std::endl;
        return;
    }

    if (listen(serverFd_, 1) < 0) {
        std::cerr << "[TLS] 소켓 Listen 실패" << std::endl;
        return;
    }

    isRunning_ = true;
    txThread_ = std::thread(&TlsServer::txThreadLoop, this);

    std::cout << "[TLS] 포트 " << serverPort_ << "에서 연결 대기 중..." << std::endl;

    while (isRunning_) {
        struct sockaddr_in clientAddr;
        socklen_t len = sizeof(clientAddr);
        int clientFd = accept(serverFd_, (struct sockaddr*)&clientAddr, &len);

        if (clientFd < 0) continue;

        {
            std::lock_guard<std::mutex> sslLock(sslMutex_);
            if (ssl_) {
                // 단일 접속(1:1) 세션 정책에 따른 기존 연결 차단 로직
                std::cerr << "[TLS] 다중 연결 시도 차단" << std::endl;
                close(clientFd);
                continue;
            }

            ssl_ = SSL_new(ctx_);
            SSL_set_fd(ssl_, clientFd);

            if (SSL_accept(ssl_) <= 0) {
                ERR_print_errors_fp(stderr);
                SSL_free(ssl_);
                ssl_ = nullptr;
                close(clientFd);
                continue;
            }
            std::cout << "[TLS] 핸드쉐이크 완료. 보안 세션 수립." << std::endl;
        }

        // 수신(RX) 루프 - 메시지 경계(\n) 기준으로 누적 파싱
        std::string rxBuffer;
        char buffer[4096];

        while (isRunning_) {
            // poll()로 소켓의 읽기 가능 여부만 먼저 확인 (sslMutex_ 미보유 상태).
            // SSL_read를 락을 쥔 채로 무한 대기시키면 TX 스레드가 SSL_write를 위해
            // sslMutex_를 얻지 못해 감지 이벤트 전송이 지연되므로, 락은 짧게만 쥔다.
            struct pollfd pfd{};
            pfd.fd = clientFd;
            pfd.events = POLLIN;
            int pollResult = poll(&pfd, 1, 200); // 200ms 주기로 재확인

            if (pollResult < 0) {
                if (errno == EINTR) continue;
                std::cerr << "[TLS] poll 오류" << std::endl;
                break;
            }
            if (pollResult == 0) {
                continue; // 타임아웃: 수신 데이터 없음. 이 사이 TX 스레드가 락을 얻어 송신 가능
            }

            int bytes = 0;
            bool connectionClosed = false;
            {
                std::lock_guard<std::mutex> sslLock(sslMutex_);
                if (!ssl_) break;
                bytes = SSL_read(ssl_, buffer, sizeof(buffer) - 1);
                if (bytes <= 0) {
                    connectionClosed = true;
                    SSL_free(ssl_);
                    ssl_ = nullptr;
                }
            }

            if (connectionClosed) {
                std::cout << "[TLS] 클라이언트 연결 종료" << std::endl;
                close(clientFd);
                break;
            }

            buffer[bytes] = '\0';
            rxBuffer.append(buffer, bytes);

            // 개행(\n)으로 구분된 완전한 JSON 메시지만 추출하여 파싱 (TCP 스트림 분절 대응)
            size_t pos;
            while ((pos = rxBuffer.find('\n')) != std::string::npos) {
                std::string line = rxBuffer.substr(0, pos);
                rxBuffer.erase(0, pos + 1);
                if (line.empty()) continue;

                try {
                    // 수동 제어 명령(환기팬 PWM 제어, 솔레노이드 밸브 잠금 등) 파싱
                    json parsedCmd = json::parse(line);
                    if (parsedCmd.contains("type") && parsedCmd["type"] == "control") {
                        std::cout << "[TLS] 제어 명령 수신: " << parsedCmd.dump() << std::endl;

                        // 하드웨어 제어 스레드(STM 보드 제어)가 waitAndPopRxCommand()로 소비
                        {
                            std::lock_guard<std::mutex> rxLock(rxMutex_);
                            rxQueue_.push(parsedCmd);
                        }
                        rxCv_.notify_one();
                    }
                } catch (const json::parse_error& e) {
                    std::cerr << "[TLS] JSON 파싱 오류: " << e.what() << std::endl;
                }
            }
        }
    }
}
