#pragma once
#include <atomic>
#include <string>
#include "judgement.h"

// update() 결과. "무엇을 해야 하는지"만 알려주고 DB·액추에이터는 안 만짐
struct AlarmOutcome {
    Judgement j;            // 강제 전환까지 반영된 최종 판정
    int  warnRemain;        // 남은 초. -1 = JSON 미포함(warning 아님), 0 = 카운트다운 종료
    long incidentId;        // 현재 사태 번호 (0 = 사태 없음)
    bool warnEntered;       // 경고 진입 순간 → event_log(warning) + 스냅샷
    bool dangerEntered;     // 위험 진입 or 원인 변경 → 대응 실행 + event_log(danger)
    bool released;          // 위험 해제 → 복귀 대응 + resolveIncident + event_log(resolve)
    long durationMs;        // released일 때 위험 지속시간
};

// 경고 무응답 타이머 + 강제 전환 + 사태(incident) 생명주기
class AlarmState {
public:
    // 1초마다 호출. 판단 결과를 넣으면 최종 상태와 할 일을 돌려줌
    AlarmOutcome update(const Judgement& j, long nowTs);

    // Qt에서 warning_ack 수신 시 호출 (수신 스레드에서 부름)
    void onWarningAck() { ack_ = true; }

private:
    static constexpr int WARN_TIMEOUT = 10;   // 무응답 자동 전환까지 (초)

    long warnStartTs_  = -1;      // 경고 진입 시각 (-1 = 타이머 비활성)
    std::atomic<bool> ack_{false};// 관리자 확인 수신 플래그
    bool ackLogged_    = false;   // 확인 로그 중복 방지
    bool forcedDanger_ = false;   // 무응답으로 강제 위험 전환된 상태

    long incidentId_      = 0;    // 현재 위험 사태 번호
    long incidentSeq_     = 0;    // 사태 번호 발급용 카운터
    long incidentStartTs_ = 0;    // 사태 진입 시각 — duration 계산용

    std::string prevState_ = "safe";
    std::string prevCause_ = "";
};