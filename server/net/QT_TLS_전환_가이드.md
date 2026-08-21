# Qt 클라이언트 TLS 전환 가이드 (서버 측 확인 완료, 반영 요청)

## 배경

`feature/tls` 브랜치에서 서버 측 TLS(`TlsServer` + `link_tls.cpp`) 구현 완료 후, 라즈베리파이(`daehongdan`, `gwang` 계정)에서 아래 절차로 직접 검증했습니다.

- `-DENABLE_TLS=ON -DENABLE_SMOKE_NCNN=OFF`로 빌드 성공
- 테스트용 자체서명 인증서 생성 후 서버 기동 → `openssl s_client -connect localhost:9999`로 접속
- TLS 1.3 핸드셰이크 성공 확인 (`[TLS] 핸드쉐이크 완료. 보안 세션 수립.`)
- 실시간 센서/카메라/액추에이터 JSON이 암호화 채널로 정상 스트리밍되는 것 확인
- 수동으로 `query` 메시지 전송 → `query_result` 응답 정상 왕복 확인 (양방향 통신 검증 완료)

**서버 쪽 TLS 계층 자체는 검증 완료 상태입니다.** 다만 `qt_client/src/core/ServerConfig.h`는 원래 이 파일을 담당하시던 분(코드 주석에 "광렬님"으로 명시)이 계셔서, Claude가 직접 수정하지 않고 **적용할 값만 정리**해서 넘겨드립니다.

## 적용해야 할 변경 사항

파일: `qt_client/src/core/ServerConfig.h`

### 1. `kUseTls` — `false` → `true`

```cpp
constexpr bool kUseTls = true;
```

### 2. `kServerCertSha256` — 서버 인증서 SHA-256 지문 채우기

```cpp
inline const QString kServerCertSha256 =
    "44:CB:3D:77:23:21:81:65:FF:E1:F2:9C:C9:80:45:0A:9A:4E:0D:7A:61:8F:E6:D0:FE:0D:79:C2:92:CD:C1:CD";
```

⚠️ 이 지문은 원래 파이의 `server/build/` 안에 즉석 생성했던 테스트 인증서 기준이었는데, PR 리뷰(구대홍님) 반영으로 인증서 경로가 **`server/net/server_cert.pem`/`server_private.pem` 고정 경로**로 바뀌었습니다 (`CMakeLists.txt`의 `TLS_CERT_PATH`/`TLS_KEY_PATH`).

기존 테스트 인증서 파일을 그 경로로 그대로 옮기기만 하면 **지문은 안 바뀌니 이 값 그대로 재사용 가능**합니다 (지문은 파일 내용 기준이라 위치와 무관):
```bash
mv server/build/server_cert.pem server/net/server_cert.pem
mv server/build/server_private.pem server/net/server_private.pem
```
새로 발급하는 경우에만 지문을 다시 뽑아야 합니다:
```bash
openssl x509 -in server/net/server_cert.pem -noout -fingerprint -sha256
```

### 3. `kServerHost` — 확인 필요 (IP 드리프트 발견)

현재 코드값 `172.20.32.41`이 파이의 실제 LAN IP(`172.20.35.185`, 2026-08-21 `hostname -I` 기준)와 **불일치**하는 걸 확인했습니다. DHCP라 그새 바뀐 것으로 보입니다. 배포/테스트 시점에 다시 확인해서 반영해주세요:

```bash
# 파이에서
hostname -I
```
가능하면 파이에 고정 IP 또는 DHCP 예약을 걸어두면 이 문제가 재발하지 않습니다 (인프라 작업, 별도 검토 필요).

## 참고 — 인증서 경로 (서버 쪽, 확정됨)

`server/net/tls_server.cpp`의 `configureContext()`가 `TLS_CERT_PATH`/`TLS_KEY_PATH` compile definition을 사용하도록 변경됐고, 이 값은 `server/net/server_cert.pem`/`server_private.pem` 고정 경로를 가리킵니다 (`CMakeLists.txt`). 더 이상 실행 디렉터리(CWD)에 의존하지 않습니다. 이 두 파일은 `.gitignore`에 등록돼 있어 git엔 안 올라가니, 배포하는 파이마다 직접 배치해야 합니다.

## 검증 재현 방법 (필요 시)

```bash
# 인증서가 server/net/에 없으면 먼저 배치 (테스트용 예시)
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout server/net/server_private.pem -out server/net/server_cert.pem \
  -days 365 -subj "/CN=daehongdan"

# 서버 (파이) — 경로가 고정됐으니 어느 디렉터리에서 실행해도 무방
cd server/build
./server_main

# 클라이언트 역할 시뮬레이션 (별도 터미널)
openssl s_client -connect localhost:9999
# 접속 후 아무 JSON 라인이나 입력해서 왕복 확인 가능, 예:
{"type":"query","target":"ignore_regions","channel":1}
```

작성: Claude (dev-grkim 세션), 2026-08-21
