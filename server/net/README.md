# net — Qt 연결 (평문 TCP / TLS)

## 📌 개요

- Qt 관제 클라이언트와의 소켓 통신 담당
- 평문 TCP 와 TLS 를 **같은 인터페이스로 교체** 가능
- 빌드 옵션 `ENABLE_TLS` 로 결정 (기본 OFF)

---

## ⚙️ 동작

```
server_main ──▶ Link (인터페이스)
                 ├── link_plain.cpp   평문 TCP
                 └── link_tls.cpp     TLS  (tls_server.cpp 사용)
```

**인터페이스로 가른 이유** — TLS 는 `recv()` 대신 `SSL_read()` 를 써야 합니다. 소켓 fd 를 밖으로
꺼내 쓰는 구조였다면 교체가 불가능했습니다. 그래서 `Link` 가 `recvLine()` 까지 감싸고,
**fd 는 모듈 밖으로 나가지 않습니다.**

| 함수 | 역할 |
| --- | --- |
| `start(port)` | listen + 내부 송수신 스레드 기동 |
| `send(line)` | **큐에 넣고 즉시 리턴** — Qt가 느려도 감지·센서 스레드가 안 멈춤 |
| `recvLine(out)` | 한 줄 꺼냄 (블로킹) |
| `connected()` · `stop()` | 상태 확인 · 종료 |

- 접속 정책은 **단일 접속(1:1)** — 새 연결이 오면 기존 연결을 대체
- 이 모듈은 「스레드는 `server_main` 만 생성」 규칙의 **예외**입니다. 블로킹 소켓 I/O라
  내부에 송수신 스레드가 필요합니다

---

## 📁 주요 파일

| 파일 | 역할 |
| --- | --- |
| `link.h` | 평문/TLS 공통 인터페이스 · `CreateLink()` |
| `link_plain.cpp` | 평문 TCP 구현 |
| `link_tls.cpp` | TLS 구현 |
| `tls_server.cpp/h` | OpenSSL 컨텍스트 · 핸드셰이크 · 인증서 로드 |
| `server_cert.pem` | 서버 인증서 — 장비마다 배치, 커밋 안 함 |
| `server_private.pem` | 개인키 — **절대 커밋 금지** |

---

## 🔧 빌드·실행

**필요** — `libssl-dev` (TLS 사용 시)

```bash
sudo apt install libssl-dev
cmake -S server -B server/build -DENABLE_TLS=ON
cmake --build server/build -j4
```

- 기본값은 `OFF` (평문 TCP)
- Qt 쪽도 `ServerConfig::kUseTls` 가 맞춰져 있어야 합니다
- 포트는 `QT_LINK_PORT` 로 주입됩니다 (기본 9999)

**인증서 배치**

`.gitignore` 대상이라 저장소에 없습니다. 장비마다 직접 넣어야 합니다.

```
server/net/server_cert.pem
server/net/server_private.pem     chmod 600
```

경로는 CMake 가 주입합니다 (`TLS_CERT_PATH` · `TLS_KEY_PATH`).

---

## 🛠 문제 해결

### 접속 확인

```bash
openssl s_client -connect <서버IP>:9999 -showcerts
# CN = daehongdan 이 보이면 TLS 정상
```

- `Connection refused` — 서버가 안 떴거나 `ENABLE_TLS` 없이 빌드된 상태
- Qt 는 인증서 지문(SHA-256)을 고정 검증합니다. 인증서를 새로 만들면
  Qt 쪽 `kServerCertSha256` 도 같이 갱신해야 합니다

### 포트가 안 풀림

서버를 종료해도 포트 9999 가 잡혀 있는 경우가 있습니다.

```bash
ss -ltnp | grep :9999
pkill -f 'ffmpeg.*8554'      # 자식 ffmpeg 가 리슨 소켓을 상속한 상태
```

> ⚠️ `socket()` 에 `SOCK_CLOEXEC` 이 없어, `fork()`+`execvp()` 로 띄운 ffmpeg 가
> 리슨 소켓을 물려받습니다. 서버만 종료해도 포트가 남습니다.

---

## 🔗 참고

- Qt 송수신 JSON 형식 — 통신 명세서
- 상위 개요 — [server/README.md](../README.md)
