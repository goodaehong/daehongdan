#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define PORT 8443

// OpenSSL 에러 출력을 위한 도움 함수
void init_openssl() {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
}

void cleanup_openssl() {
    EVP_cleanup();
}

// SSL 컨텍스트 생성 및 인증서 로드
SSL_CTX* create_context() {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = TLS_server_method(); // 최신 TLS 표준 프로토콜 사용
    ctx = SSL_CTX_new(method);
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    return ctx;
}

void configure_context(SSL_CTX *ctx) {
    // 1단계에서 만든 인증서(crt)와 개인키(key) 경로를 지정합니다.
    if (SSL_CTX_use_certificate_file(ctx, "server.crt", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "server.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
}

int main() {
    int server_fd;
    struct sockaddr_in addr;

    init_openssl();
    SSL_CTX *ctx = create_context();
    configure_context(ctx);

    // 1. 표준 TCP 소켓 생성 및 바인딩
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Unable to create socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Unable to bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 1) < 0) {
        perror("Unable to listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "C++ TLS 서버가 " << PORT << " 포트에서 대기 중입니다..." << std::endl;

    // 2. 연결 대기 루프
    while (true) {
        struct sockaddr_in client_addr;
        uint len = sizeof(client_addr);
        
        // TCP 연결 수락 (일반 소켓 통신 완료)
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            perror("Unable to accept");
            continue;
        }

        std::cout << "클라이언트 연결 감지! TLS 핸드셰이크를 시작합니다." << std::endl;

        // 일반 TCP 소켓 위에 SSL 구조체 바인딩
        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client_fd);

        // TLS 핸드셰이크 수행 (SSL_accept)
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            std::cout << "TLS 핸드셰이크 실패!" << std::endl;
        } else {
            std::cout << "보안 터널(TLS) 연결 성공!" << std::endl;

            // 암호화 데이터 수신 (SSL_read)
            char buf[1024] = {0};
            int bytes = SSL_read(ssl, buf, sizeof(buf) - 1);
            if (bytes > 0) {
                std::cout << "받은 암호화 데이터: " << buf << std::endl;
                
                // 암호화 데이터 송신 (SSL_write)
                const char reply[] = "C++ TLS Server Response Success!";
                SSL_write(ssl, reply, sizeof(reply));
            }
        }

        // 연결 종료 및 자원 해제
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_fd);
    }

    close(server_fd);
    SSL_CTX_free(ctx);
    cleanup_openssl();
}