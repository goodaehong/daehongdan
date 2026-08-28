#include "tls_server.h"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <cerrno>
#include <csignal>

TlsServer::TlsServer(int port) : serverPort_(port), ctx_(nullptr), ssl_(nullptr), isRunning_(false), serverFd_(-1) {
    initOpenSSL();
    ctx_ = createContext();
    ready_ = ctx_ && configureContext(ctx_);
}

TlsServer::~TlsServer() {
    stop();
    cleanupOpenSSL();
}

// run()의 accept()는 타임아웃 없이 블로킹이라, serverFd_를 닫아서 깨운다
// (Linux에서 accept 중인 fd를 다른 스레드가 close하면 EBADF로 즉시 반환됨)
void TlsServer::stop() {
    if (!isRunning_.exchange(false)) return;
    txCv_.notify_all(); // 대기 중인 TX 스레드 종료 신호 전달
    rxCv_.notify_all(); // 대기 중인 RX 소비자(recvLine 호출자) 종료 신호 전달

    if (txThread_.joinable()) {
        txThread_.join();
    }
    {
        std::lock_guard<std::mutex> sslLock(sslMutex_);
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
    }
    connected_ = false;
    if (serverFd_ >= 0) {
        close(serverFd_);
        serverFd_ = -1;
    }
}

void TlsServer::initOpenSSL() {
    // OpenSSL의 소켓 BIO는 상대가 연결을 끊은 직후 SSL_write()가 실행되면
    // SIGPIPE를 발생시킬 수 있다. 기본 동작은 프로세스 종료이므로 서버 전체가
    // Qt의 일시적인 연결 해제 하나로 죽지 않도록 무시하고 반환값으로 처리한다.
    std::signal(SIGPIPE, SIG_IGN);
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
        return nullptr;
    }
    // TLS 1.2 미만(SSLv3/TLS1.0/1.1) 협상 차단
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    return ctx;
}

// 실패 시 exit() 대신 false 반환 — 호출자(생성자)가 ready_로 기록하고
// TlsLink::start()가 Link::start()의 "실패 시 false" 계약을 지킬 수 있게 한다
bool TlsServer::configureContext(SSL_CTX* ctx) {
    // 경로는 CMakeLists.txt의 TLS_CERT_PATH/TLS_KEY_PATH로 고정 (실행 CWD 종속 방지)
    if (SSL_CTX_use_certificate_file(ctx, TLS_CERT_PATH, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return false;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, TLS_KEY_PATH, SSL_FILETYPE_PEM) <= 0 ) {
        ERR_print_errors_fp(stderr);
        return false;
    }
    // 인증서와 개인키가 서로 짝이 맞는지 검증 (안 맞으면 핸드셰이크 때가 돼서야 실패로 드러남)
    if (!SSL_CTX_check_private_key(ctx)) {
        std::cerr << "[TLS] 인증서와 개인키가 일치하지 않음" << std::endl;
        return false;
    }
    return true;
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
            // SSL_write가 요청량보다 적게 쓰고 돌아올 수 있으므로 다 나갈 때까지 반복
            // (link_plain.cpp의 send() 루프와 동일한 이유 — 잘리면 \n 프레이밍이 어긋남)
            size_t sent = 0;
            while (sent < dataToSend.size()) {
                int bytes = SSL_write(ssl_, dataToSend.data() + sent, dataToSend.size() - sent);
                if (bytes <= 0) {
                    std::cerr << "[TLS] 데이터 송신 실패" << std::endl;
                    break;
                }
                sent += (size_t)bytes;
            }
        }
    }
}

bool TlsServer::waitAndPopRxLine(std::string& outLine) {
    std::unique_lock<std::mutex> lock(rxMutex_);
    rxCv_.wait(lock, [this] { return !rxQueue_.empty() || !isRunning_; });

    if (rxQueue_.empty()) {
        return false; // 서버 종료로 인해 깨어남
    }

    outLine = rxQueue_.front();
    rxQueue_.pop();
    return true;
}

void TlsServer::run() {
    if (!ready_) {
        std::cerr << "[TLS] 인증서/키 초기화 실패로 실행할 수 없음" << std::endl;
        return;
    }

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
            connected_ = true;
            std::cout << "[TLS] 핸드쉐이크 완료. 보안 세션 수립." << std::endl;
        }

        // 수신(RX) 루프 - 메시지 경계(\n) 기준으로 누적 파싱
        std::string rxBuffer;
        char buffer[4096];

        while (isRunning_) {
            // poll()로 소켓의 읽기 가능 여부만 먼저 확인 (sslMutex_ 미보유 상태).
            // SSL_read를 락을 쥔 채로 무한 대기시키면 TX 스레드가 SSL_write를 위해
            // sslMutex_를 얻지 못해 감지 이벤트 전송이 지연되므로, 락은 짧게만 쥔다.
            //
            // serverFd_(리스닝 소켓)도 같이 감시한다 — 안 그러면 이 안쪽 루프에 머무는 동안
            // accept()가 다시 호출될 일이 없어서, 첫 연결이 살아있는 동안 두 번째 접속 시도를
            // 걸러내는 "단일 접속 차단" 체크(바깥 루프에 있음)에 영영 도달하지 못한다.
            struct pollfd pfds[2];
            pfds[0].fd = clientFd;      pfds[0].events = POLLIN; pfds[0].revents = 0;
            pfds[1].fd = serverFd_;     pfds[1].events = POLLIN; pfds[1].revents = 0;
            int pollResult = poll(pfds, 2, 200); // 200ms 주기로 재확인

            if (pollResult < 0) {
                if (errno == EINTR) continue;
                std::cerr << "[TLS] poll 오류" << std::endl;
                {
                    std::lock_guard<std::mutex> sslLock(sslMutex_);
                    if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
                }
                connected_ = false;
                close(clientFd);
                break;
            }
            if (pollResult == 0) {
                continue; // 타임아웃: 수신 데이터 없음. 이 사이 TX 스레드가 락을 얻어 송신 가능
            }

            // 새 접속 시도 감지 — 기존 세션은 그대로 두고 새 연결만 즉시 거부
            if (pfds[1].revents & POLLIN) {
                struct sockaddr_in newAddr;
                socklen_t newLen = sizeof(newAddr);
                int newFd = accept(serverFd_, (struct sockaddr*)&newAddr, &newLen);
                if (newFd >= 0) {
                    std::cerr << "[TLS] 다중 연결 시도 차단" << std::endl;
                    close(newFd);
                }
            }

            // 정상 종료뿐 아니라 네트워크 단절·클라이언트 강제 종료도 즉시
            // 세션 정리 대상으로 처리한다. 이를 무시하면 ssl_이 남아서 이후
            // Qt 재접속이 계속 "다중 연결"로 거부될 수 있다.
            if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                {
                    std::lock_guard<std::mutex> sslLock(sslMutex_);
                    if (ssl_) {
                        SSL_shutdown(ssl_);
                        SSL_free(ssl_);
                        ssl_ = nullptr;
                    }
                }
                connected_ = false;
                close(clientFd);
                std::cout << "[TLS] 클라이언트 연결 종료" << std::endl;
                break;
            }

            if (!(pfds[0].revents & POLLIN)) {
                continue; // 이번 poll은 새 접속 감지뿐, 기존 세션엔 읽을 데이터 없음
            }

            int bytes = 0;
            bool connectionClosed = false;
            {
                std::lock_guard<std::mutex> sslLock(sslMutex_);
                if (!ssl_) break;
                bytes = SSL_read(ssl_, buffer, sizeof(buffer) - 1);
                if (bytes <= 0) {
                    connectionClosed = true;
                    SSL_shutdown(ssl_);   // 가능하면 close_notify 전달 (한 번만 시도, 응답 대기 안 함)
                    SSL_free(ssl_);
                    ssl_ = nullptr;
                }
            }

            if (connectionClosed) {
                connected_ = false;
                std::cout << "[TLS] 클라이언트 연결 종료" << std::endl;
                close(clientFd);
                break;
            }

            buffer[bytes] = '\0';
            rxBuffer.append(buffer, bytes);

            if (rxBuffer.size() > MAX_LINE) {
                std::cerr << "[TLS] 수신 줄이 상한 초과 — 연결 끊음" << std::endl;
                {
                    std::lock_guard<std::mutex> sslLock(sslMutex_);
                    if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
                }
                connected_ = false;
                close(clientFd);
                break;
            }

            // 개행(\n)으로 구분된 완전한 메시지만 원문 그대로 큐에 적재 (TCP 스트림 분절 대응)
            // 타입 판별·파싱은 하지 않는다 — control/emergency/query 등 라우팅은 qt_link.cpp가 담당
            size_t pos;
            while ((pos = rxBuffer.find('\n')) != std::string::npos) {
                std::string line = rxBuffer.substr(0, pos);
                rxBuffer.erase(0, pos + 1);
                if (line.empty()) continue;

                {
                    std::lock_guard<std::mutex> rxLock(rxMutex_);
                    rxQueue_.push(line);
                }
                rxCv_.notify_one();
            }
        }
    }
}
