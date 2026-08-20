#include "link.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cerrno>

// 평문 TCP. Qt가 직접 접속(listen 모드). TLS 켜면 link_tls.cpp로 교체됨
class PlainLink : public Link {
public:
    ~PlainLink() override { stop(); }

    bool start(int port) override {
        listenfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenfd_ < 0) return false;
        int opt = 1;
        ::setsockopt(listenfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (::bind(listenfd_, (sockaddr*)&addr, sizeof(addr)) < 0) { ::close(listenfd_); listenfd_ = -1; return false; }
        if (::listen(listenfd_, 1) < 0)                            { ::close(listenfd_); listenfd_ = -1; return false; }

        running_ = true;
        ioThread_ = std::thread(&PlainLink::ioLoop, this);
        txThread_ = std::thread(&PlainLink::txLoop, this);
        std::cout << "[링크] 평문 TCP 대기 중 (포트 " << port << ")\n";
        return true;
    }

    // 큐에 넣고 즉시 리턴. Qt가 느려도 호출한 스레드가 안 막힘
    void send(const std::string& line) override {
        {
            std::lock_guard<std::mutex> lk(txMtx_);
            if (txQueue_.size() >= MAX_QUEUE) txQueue_.pop();   // Qt 끊긴 채 쌓이면 오래된 것부터 버림
            txQueue_.push(line);
        }
        txCv_.notify_one();
    }

    // 한 줄 꺼냄. 없으면 대기. stop() 되면 false
    bool recvLine(std::string& out) override {
        std::unique_lock<std::mutex> lk(rxMtx_);
        rxCv_.wait(lk, [&]{ return !rxQueue_.empty() || !running_; });
        if (rxQueue_.empty()) return false;
        out = rxQueue_.front();
        rxQueue_.pop();
        return true;
    }

    bool connected() const override { return clientfd_ >= 0; }

    void stop() override {
        if (!running_.exchange(false)) return;
        txCv_.notify_all();
        rxCv_.notify_all();
        if (ioThread_.joinable()) ioThread_.join();
        if (txThread_.joinable()) txThread_.join();
        if (clientfd_ >= 0) { ::close(clientfd_); clientfd_ = -1; }
        if (listenfd_ >= 0) { ::close(listenfd_); listenfd_ = -1; }
    }

private:
    // accept + 수신. poll 200ms라 stop() 걸리면 그 안에 빠져나옴
    void ioLoop() {
        std::string buf;   // \n 프레이밍용 (TCP는 메시지 경계가 없음)
        while (running_) {
            pollfd fds[2];
            int n = 0;
            fds[n++] = { listenfd_, POLLIN, 0 };
            int cfd = clientfd_;
            if (cfd >= 0) fds[n++] = { cfd, POLLIN, 0 };

            if (::poll(fds, n, 200) <= 0) continue;

            // 새 접속 — 이전(죽은) 연결은 닫고 교체 (단일 접속 정책)
            if (fds[0].revents & POLLIN) {
                int newfd = ::accept(listenfd_, nullptr, nullptr);
                if (newfd >= 0) {
                    std::lock_guard<std::mutex> lk(fdMtx_);
                    if (clientfd_ >= 0) ::close(clientfd_);
                    clientfd_ = newfd;
                    buf.clear();
                    std::cout << "[링크] Qt 접속됨\n";
                }
            }

            // 수신
            if (n == 2 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
                char tmp[4096];
                ssize_t r = ::recv(cfd, tmp, sizeof(tmp), 0);
                if (r <= 0) {
                    std::lock_guard<std::mutex> lk(fdMtx_);
                    ::close(clientfd_); clientfd_ = -1;
                    std::cout << "[링크] Qt 연결 끊김\n";
                    continue;
                }
                buf.append(tmp, r);
                if (buf.size() > MAX_LINE) {                                   
                    std::cerr << "[링크] 수신 줄이 상한 초과 — 연결 끊음\n";
                    std::lock_guard<std::mutex> lk(fdMtx_);
                    ::close(clientfd_); clientfd_ = -1;
                    buf.clear();
                    continue;
                }             
                size_t pos;
                while ((pos = buf.find('\n')) != std::string::npos) {
                    std::string line = buf.substr(0, pos);
                    buf.erase(0, pos + 1);
                    if (line.empty()) continue;
                    {
                        std::lock_guard<std::mutex> lk(rxMtx_);
                        rxQueue_.push(line);
                    }
                    rxCv_.notify_one();
                }
            }
        }
    }

    // 송신 전용. 큐에서 꺼내 실제 소켓으로
    void txLoop() {
        while (running_) {
            std::string line;
            {
                std::unique_lock<std::mutex> lk(txMtx_);
                txCv_.wait(lk, [&]{ return !txQueue_.empty() || !running_; });
                if (!running_) break;
                line = txQueue_.front();
                txQueue_.pop();
            }
            std::lock_guard<std::mutex> lk(fdMtx_);
            if (clientfd_ < 0) continue;   // 아직 아무도 안 붙음 → 버림
            std::string out = line + "\n";
            // send는 요청량보다 적게 보내고 돌아올 수 있다. 한 줄이 잘리면  
            // 그 뒤 메시지 경계가 전부 어긋나므로 다 나갈 때까지 반복한다
            size_t sent = 0;
            bool ok = true;
            while (sent < out.size()) {
                ssize_t n = ::send(clientfd_, out.data() + sent,
                                   out.size() - sent, MSG_NOSIGNAL);
                if (n > 0) { sent += (size_t)n; continue; }
                if (n < 0 && errno == EINTR) continue;   // 시그널 중단 → 재시도
                ok = false;
                break;
            }
            if (!ok) {
                ::close(clientfd_); clientfd_ = -1;
            }                                                                  
        }
    }

    static constexpr size_t MAX_QUEUE = 1000;   // Qt 끊긴 채 무한히 쌓이는 것 방지
    static constexpr size_t MAX_LINE = 8 * 1024 * 1024;   // 평면도 base64 여유분  

    int listenfd_ = -1;
    std::atomic<int> clientfd_{-1};
    std::mutex fdMtx_;                          // clientfd_ 쓰기·소켓 I/O 보호

    std::queue<std::string> txQueue_;
    std::mutex txMtx_;
    std::condition_variable txCv_;
    std::thread txThread_;

    std::queue<std::string> rxQueue_;
    std::mutex rxMtx_;
    std::condition_variable rxCv_;
    std::thread ioThread_;

    std::atomic<bool> running_{false};
};

// CMake가 link_plain.cpp / link_tls.cpp 중 하나만 빌드에 넣음
Link* CreateLink() { return new PlainLink(); }