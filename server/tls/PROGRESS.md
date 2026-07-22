# TLS 서버 작업 현황 (feature/tls)

## 완료된 부분

- `TlsServer` 클래스로 리팩터링 (`main.cpp` → `TlsServer.cpp`/`TlsServer.h`)
- 인증서/키 로드(`server_cert.pem`, `server_private.pem`), TLS 핸드셰이크, 단일 접속(1:1) 정책
- 송신 전용 스레드 + 스레드 안전 큐 (`enqueueTxData`) — 외부 스레드에서 자유롭게 호출 가능
- 수신 루프 non-blocking 처리(`poll` 200ms 타임아웃)로 TX 스레드 starvation 문제 해결
- 수신 메시지 `\n` 기준 프레이밍 (TCP 스트림 분절 대응)
- 수신 제어 명령 큐 (`waitAndPopRxCommand`) — 하드웨어 제어 스레드가 소비할 producer-consumer 구조
- 팀 합의 5개 항목(인증서 경로 / 수신 큐 구조 / 송신 동기화 / 포트 9999 / 단일 접속) 대조 검토 완료 — 전부 충족
- 구버전 잔재 파일(`main.cpp`, `server.crt`, 안 쓰는 `server.key`) 정리
- 라즈베리파이(`gwangbox`)에서 컴파일 확인 완료

## 남은 과제

1. **통합 `main.cpp` 부재** — `TlsServer`가 아직 `stream_receiver.cpp`, 하드웨어 제어 스레드와 묶여서 실행되는 진입점이 없음
   - `stream_receiver.cpp`의 `Sender` 클래스(평문 소켓, mock 9999포트) 제거
   - `worker()`가 `sender.send()` 대신 `tlsServer.enqueueTxData()` 직접 호출하도록 변경
   - `CMakeLists.txt`에 OpenSSL + OpenCV 동시 링크 설정 필요
2. **RX 큐 소비자 없음** — `waitAndPopRxCommand()`로 꺼낸 제어 명령을 실제 STM UART 드라이버 스레드가 처리하도록 연결 필요 (김유나 파트와 조율)
3. **Qt 클라이언트 측 TLS와 연동 테스트 미완료** — 장태호 님 클라이언트 TLS 작업 완료 후 실제 핸드셰이크 통합 테스트 필요
4. **보안 강화 항목 (저우선순위)**
   - `SSL_CTX_set_min_proto_version`으로 최소 TLS 버전 미지정
   - `SSL_CTX_check_private_key`로 인증서/키 일치 검증 없음
   - 연결 종료 시 `SSL_shutdown()` 없이 바로 `close()` (graceful shutdown 미적용)
