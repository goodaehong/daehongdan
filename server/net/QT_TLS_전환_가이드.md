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

⚠️ **이 지문은 임시 테스트용입니다.** 파이의 `server/build/` 안에 `openssl req -x509 ...`로 즉석 생성한 자체서명 인증서 기준이라:
- `rm -rf build`나 cmake 재구성 시 인증서가 같이 사라질 수 있음
- 실배포 인증서는 `build/` 밖 고정 위치(예: `server/net/` 등)로 옮기고 나서, 그 인증서 기준으로 지문을 **다시 뽑아서** 이 값을 갱신해야 합니다:
  ```bash
  openssl x509 -in <실제 배포용 인증서 경로> -noout -fingerprint -sha256
  ```

### 3. `kServerHost` — 확인 필요 (IP 드리프트 발견)

현재 코드값 `172.20.32.41`이 파이의 실제 LAN IP(`172.20.35.185`, 2026-08-21 `hostname -I` 기준)와 **불일치**하는 걸 확인했습니다. DHCP라 그새 바뀐 것으로 보입니다. 배포/테스트 시점에 다시 확인해서 반영해주세요:

```bash
# 파이에서
hostname -I
```
가능하면 파이에 고정 IP 또는 DHCP 예약을 걸어두면 이 문제가 재발하지 않습니다 (인프라 작업, 별도 검토 필요).

## 참고 — 인증서 경로 (서버 쪽, 아직 미확정)

`TlsServer`는 실행 디렉터리 기준 상대경로로 `server_cert.pem`/`server_private.pem`을 찾습니다 (`server/net/TlsServer.cpp`의 `configureContext()`). 실배포 시 이 경로를 고정 절대경로로 바꾸는 작업이 `server/net/PROGRESS.md`에 남은 과제로 정리돼 있습니다 — 인증서 최종 배치 위치가 정해지면 그때 같이 처리하면 됩니다.

## 검증 재현 방법 (필요 시)

```bash
# 서버 (파이)
cd server/build
./server_main

# 클라이언트 역할 시뮬레이션 (별도 터미널)
openssl s_client -connect localhost:9999
# 접속 후 아무 JSON 라인이나 입력해서 왕복 확인 가능, 예:
{"type":"query","target":"ignore_regions","channel":1}
```

작성: Claude (dev-grkim 세션), 2026-08-21
