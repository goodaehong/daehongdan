# TLS 서버 작업 현황 (feature/tls)

## 완료된 부분

- `TlsServer` 클래스로 리팩터링 (`main.cpp` → `TlsServer.cpp`/`TlsServer.h`)
- 인증서/키 로드(`server_cert.pem`, `server_private.pem`), TLS 핸드셰이크, 단일 접속(1:1) 정책
- 송신 전용 스레드 + 스레드 안전 큐 (`enqueueTxData`) — 외부 스레드에서 자유롭게 호출 가능
- 수신 루프 non-blocking 처리(`poll` 200ms 타임아웃)로 TX 스레드 starvation 문제 해결
- 수신 메시지 `\n` 기준 프레이밍 (TCP 스트림 분절 대응)
- 팀 합의 5개 항목(인증서 경로 / 수신 큐 구조 / 송신 동기화 / 포트 9999 / 단일 접속) 대조 검토 완료 — 전부 충족
- 구버전 잔재 파일(`main.cpp`, `server.crt`, 안 쓰는 `server.key`) 정리
- 라즈베리파이(`gwangbox`)에서 컴파일 확인 완료 (리팩터링 전 버전 기준)
- **`Link` 인터페이스 결합 완료** (`server/net/link_tls.cpp` 신설)
  - main 쪽에서 이미 `server_main.cpp`가 `stream_receiver.cpp`/`Sender` 대신 `Link&` 기반 아키텍처로 넘어가 있었음 — 그 인터페이스에 `TlsLink : public Link`로 얹는 방식으로 통합 (당초 계획이던 "stream_receiver.cpp 직접 결합"은 이미 낡은 전제였음)
  - `TlsServer::run()`을 `TlsLink::start()`에서 별도 스레드로 기동, `enqueueTxData`/`connected()`/`stop()`을 `Link`에 매핑
  - RX 큐를 파싱된 JSON(`control` 타입만 필터링)에서 **원문 라인 큐**(`waitAndPopRxLine`)로 변경 — 타입 판별 없이 다 큐잉하고, 라우팅은 `qt_link.cpp`의 `QtLink_RecvWorker`가 담당 (기존 `PlainLink` 경로와 동일한 방식). 기존 방식은 `control` 외 타입(`emergency`/`set_ignore_regions`/`set_floor_map`/`query`/`warning_ack`)이 전부 유실되는 문제가 있었음
  - `SSL_write` partial-write 대응(다 나갈 때까지 반복 전송), 수신 버퍼 8MB 상한 추가 — `link_plain.cpp`에 이미 적용된 견고성 패치와 동일 패턴

## 남은 과제

1. **Qt 클라이언트 측 TLS와 연동 테스트 미완료** — 장태호 님 클라이언트 TLS 작업 완료 후 실제 핸드셰이크 통합 테스트 필요. `link_tls.cpp` 결합은 코드 리뷰 수준으로만 검증됐고, 라즈베리파이 빌드·실제 Qt 접속 테스트는 아직 안 함
2. **`TlsServer::start()`가 실패를 `false`로 알리지 못함** — 인증서 로드 실패 시 생성자 안에서 `exit(EXIT_FAILURE)`로 프로세스 자체가 죽음. `PlainLink`처럼 "실패해도 나머지 초기화는 계속 진행"이 안 됨 (server_main.cpp의 `if (!g_db.open(...))` 같은 관용구와 어긋남)
3. **poll() 오류 시 정리 누락** — RX 루프에서 `poll()`이 `EINTR` 이외 오류로 실패하면 `clientFd`/`ssl_`/`connected_` 정리 없이 그냥 break (기존부터 있던 gap, 이번 작업 범위에서는 안 건드림)
4. **보안 강화 항목 (저우선순위)**
   - `SSL_CTX_set_min_proto_version`으로 최소 TLS 버전 미지정
   - `SSL_CTX_check_private_key`로 인증서/키 일치 검증 없음
   - 연결 종료 시 `SSL_shutdown()` 없이 바로 `close()` (graceful shutdown 미적용)
   - 인증서 경로(`server_cert.pem`/`server_private.pem`)가 상대경로 하드코딩 — 실행 CWD에 의존
