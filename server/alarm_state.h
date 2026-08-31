#pragma once

// 경보 단계 관리. 판정 결과를 받아 경고 · 위험 · 비상 전환을 결정한다.
// 경고 무응답 타이머, 사태(incident) 생명주기, 비상 래치를 여기서 다룬다.
// 감지가 깜빡여도 경보가 흔들리지 않도록 순간값이 아닌 타이머 기준으로 판정한다.

#include <atomic>
#include <mutex>
#include <string>
#include "judgement.h"

// update() 결과. "무엇을 해야 하는지"만 알려주고 DB·액추에이터는 안 만짐
struct AlarmOutcome {
    Judgement j;               // 강제 전환·수동 발령·래치까지 반영된 최종 판정
    std::string naturalState;  // 래치 전 자연 판정. 해제 체크리스트·복귀 대응 판단용
    int  warnRemain;        // 남은 초. -1 = JSON 미포함(warning 아님), 0 = 카운트다운 종료
    long incidentId;        // 현재 사태 번호 (0 = 사태 없음)
    bool warnEntered;       // 경고 진입 → event_log(warning) + 스냅샷
    bool dangerEntered;     // 위험 진입 or 원인 변경 → 대응 실행 + event_log(danger)
    bool released;          // 사태 종료 → resolveIncident + event_log(resolve)
    bool wasDanger;         // 이 사태가 위험까지 갔었나
    long durationMs;        // released일 때 위험 지속시간
    bool emergEntered;      // 이번 tick에 수동 전환 or 대응 재실행 → 대응 실행
    bool emergReapply;      // 그중 재실행인가 (이미 위험이었음) — DB category 구분용 
    bool emergCleared;      // 이번 tick에 수동 해제
    bool manual;            // 현재 위험이 수동 발령인가 (dangerSource)
    std::string admin;      // 수동 발령자 (자동이면 빈 문자열)
};

// 경고 무응답 타이머 + 강제 전환 + 사태 생명주기 + 비상 모드 래치
class AlarmState {
public:
    // 1초마다 호출. 판단 결과를 넣으면 최종 상태와 할 일을 돌려줌
    AlarmOutcome update(const Judgement& j, long nowTs);

    // Qt에서 warning_ack 수신 시 호출 (수신 스레드)
    void onWarningAck() { ack_ = true; }

    // 비상 모드 전환(on=true)/해제(on=false) 요청. 수신 스레드에서 부름
    // 거절 없음 — 켜는 방향은 막지 않는다. 실제 실행은 다음 tick의 update()
    void requestEmergency(bool on, const std::string& cause, const std::string& admin);

    // 사태 번호는 메모리 카운터라 재시작하면 1부터 다시 나와 옛 로그와 겹친다.
    // 서버 시작 시 DB의 마지막 번호를 넣어 그 다음부터 발급하게 한다
    void setIncidentSeqStart(long lastId) {
        if (lastId > incidentSeq_) incidentSeq_ = lastId;
    }      

private:
    static constexpr int WARN_TIMEOUT = 10;   // 무응답 자동 전환까지 (초)

    long warnStartTs_  = -1;      // 경고 진입 시각 (-1 = 타이머 비활성)
    std::atomic<bool> ack_{false};// 관리자 확인 수신 플래그
    bool ackLogged_    = false;   // 확인 로그 중복 방지
    bool warnClearLogged_ = false;   // "해제됨" 로그 중복 방지 (깜빡임 도배)
    std::string warnCause_;          // 경고 진입 시 원인. 깜빡임 중 상태 유지에 씀 
    bool forcedDanger_ = false;   // 무응답으로 강제 위험 전환된 상태

    long incidentId_      = 0;    // 현재 사태 번호
    long incidentSeq_     = 0;    // 사태 번호 발급용 카운터
    long incidentStartTs_ = 0;    // 사태 진입 시각 — duration 계산용
    bool wasDanger_       = false;// 사태 중 위험 도달 여부

    std::string prevState_ = "safe";
    std::string prevCause_ = "";

    // ── 비상 모드 ── cause가 문자열이라 atomic 불가 → 뮤텍스
    std::mutex  reqMtx_;
    bool        reqTrigger_ = false;  // 대기 중인 전환 요청
    bool        reqClear_   = false;  // 대기 중인 해제 요청
    std::string reqCause_;
    std::string reqAdmin_;

    bool        manual_ = false;      // 수동 발령으로 위험을 유지 중인가
    std::string manualCause_;         // 수동 발령 시 관리자가 지정한 원인
    std::string manualAdmin_;         // 발령자
    bool        latched_ = false;     // 위험 래치 — 수동 해제 전까지 안 풀림
    std::string latchCause_;          // 래치 시점의 원인
    long        hazardEndTs_ = 0;     // 자연 판정이 안전으로 돌아온 시각 (0=아직)

    // 감지 깜빡임으로 사태가 여러 건으로 쪼개지는 것을 막는다                
    static constexpr int RELEASE_HOLD = 10;   // 안전이 이만큼 지속돼야 사태 종료
    long safeSinceTs_ = 0;                    // 안전으로 내려간 시각 (0 = 안전 아님)
};