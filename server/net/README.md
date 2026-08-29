# net — Qt 연결 (평문 TCP / TLS)

## 📌 개요

- 관제 화면(Qt)과의 소켓 통신 담당
- 평문 TCP 와 TLS 를 **같은 인터페이스로 교체** 가능
- 빌드 옵션 `ENABLE_TLS` 로 결정 (기본 OFF)

---

## ⚙️ 동작

```
server_main ──▶ Link (인터페이스)
                 ├── 🔓 link_plain.cpp   평문 TCP
                 └── 🔐 link_tls.cpp     TLS (tls_server.cpp 사용)
```

### 인터페이스로 가른 이유

| | 평문 | TLS |
| --- | --- | --- |
| 읽기 | `recv()` | `SSL_read()` |
| 쓰기 | `send()` | `SSL_write()` |
| 소켓 fd | 필요 없음 | 밖으로 꺼내면 안 됨 |

- 소켓 fd 를 밖에서 쓰는 구조였다면 교체 불가
- `Link` 가 `recvLine()` 까지 감싸서 **fd 는 모듈 밖으로 안 나감**
- 호출부는 어느 쪽으로 빌드됐는지 모름

### 제공 기능

| 함수 | 하는 일 |
| --- | --- |
| `start(port)` | 접속 대기 시작 + 내부 송수신 스레드 기동 |
| `send(line)` | **큐에 넣고 즉시 반환** — Qt 가 느려도 감지·센서 스레드가 안 멈춤 |
| `recvLine(out)` | 받은 줄 하나 꺼냄 (대기) |
| `connected()` | 접속 여부 |
| `stop()` | 종료 |

- 접속 정책은 **1:1** — 새 연결이 오면 기존 연결 대체
- 「스레드는 `server_main` 만 생성」 규칙의 **유일한 예외** — 소켓 I/O 가 대기형이라 내부 스레드 필요

---

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `link.h` | 평문·TLS 공통 인터페이스 · 생성 함수 |
| `link_plain.cpp` | 평문 TCP 구현 |
| `link_tls.cpp` | TLS 구현 |
| `tls_server.cpp/h` | 암호화 준비 · 인증서 로드 · 핸드셰이크 |
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
- Qt 쪽 `ServerConfig::kUseTls` 도 같은 값으로 맞춰야 합니다
- 포트는 `QT_LINK_PORT` 로 주입됩니다 (기본 9999)

<details>
<summary><b>인증서 배치</b> — 저장소에 없으므로 장비마다 직접 넣어야 합니다</summary>

```
server/net/server_cert.pem
server/net/server_private.pem     chmod 600
```

경로는 CMake 가 주입합니다 (`TLS_CERT_PATH` · `TLS_KEY_PATH`).

접속 확인:

```bash
openssl s_client -connect <서버IP>:9999 -showcerts
# CN = daehongdan 이 보이면 TLS 정상
```

Qt 는 인증서 지문(SHA-256)을 고정 검증합니다. 인증서를 새로 만들면
Qt 쪽 `kServerCertSha256` 도 같이 갱신해야 합니다.

</details>

---

## 🛠 문제 해결

### 서버를 껐는데 포트 9999 가 안 풀림

- **증상** — 재기동 시 접속 대기 실패
- **원인** — `socket()` 생성에 `SOCK_CLOEXEC` 이 없어, `fork()`+`execvp()` 로 띄운 ffmpeg 가 대기 소켓을 물려받음
- **확인·조치**

```bash
ss -ltnp | grep :9999
pkill -f 'ffmpeg.*8554'
```

### TLS 로 안 붙음

| 증상 | 원인 |
| --- | --- |
| `Connection refused` | 서버 미기동, 또는 `ENABLE_TLS` 없이 빌드됨 |
| 핸드셰이크 실패 | Qt 의 `kUseTls` 가 서버와 불일치 |
| 지문 불일치 | 인증서 재발급 후 Qt 상수 미갱신 |

---

## 🔗 참고

- Qt 송수신 JSON 형식 — 통신 명세서
- 상위 개요 — [server/README.md](../README.md)
