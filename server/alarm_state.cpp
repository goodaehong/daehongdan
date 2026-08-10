#include "alarm_state.h"
#include <iostream>

AlarmOutcome AlarmState::update(const Judgement& in, long nowTs) {
    AlarmOutcome o{};
    o.j = in;
    o.warnRemain = -1;   // -1 = JSON에 미포함 (warning 아닐 때)

    // ── 대피 모드: 수신 스레드가 넣어둔 요청을 여기서 반영 ──    
    int req = evacReq_.exchange(-1);
    if (req == 1 && !evacActive_) { evacActive_ = true;  o.evacEntered = true; }
    if (req == 0 &&  evacActive_) { evacActive_ = false; o.evacCleared = true; }
    o.evacActive = evacActive_;                                   

    // ── 경고 무응답 타이머: warning 지속 중 관리자 미확인 시 위험 강제 전환 ──
    if (o.j.state == "warning") {
        if (warnStartTs_ < 0) {          // warning 진입 순간
            warnStartTs_  = nowTs;
            ack_          = false;       // 새 경고 시작 → 이전 ack 무효화
            ackLogged_    = false;
            forcedDanger_ = false;
            o.warnEntered = true;
            if (incidentId_ == 0) {              // 사태 시작 (경고부터 한 사태로) 
                incidentId_      = ++incidentSeq_;
                incidentStartTs_ = nowTs;
            }  
            std::cout << "[경고] " << o.j.cause << " 발생 → 관리자 알림 ("
                      << WARN_TIMEOUT << "초 대기)\n";
        }
        if (ack_.load()) {
            if (!ackLogged_) {           // 한 번만
                std::cout << "[경고] 관리자 확인함 → 자동 전환 취소\n";
                ackLogged_ = true;
            }
            o.warnRemain = 0;            // 관리자 확인함 → 카운트다운 종료(전환 안 함)
        } else {
            o.warnRemain = WARN_TIMEOUT - (int)(nowTs - warnStartTs_);
            if (o.warnRemain <= 0) {     // 무응답 → 강제 위험 전환
                o.warnRemain = 0;
                if (!forcedDanger_)      // 전환되는 순간만 (한 번)
                    std::cout << "[경고] " << WARN_TIMEOUT << "초 무응답 → 강제 위험 전환!\n";
                forcedDanger_ = true;
            }
        }
    } else {
        if (warnStartTs_ >= 0 && o.j.state == "safe" && !forcedDanger_)   // 위험까지 안 간 경우, 안전 복귀일 때만
            std::cout << "[경고] 해제됨 (감지 사라짐)\n";
        warnStartTs_ = -1;               // warning 벗어남 → 타이머 리셋
        if (o.j.state == "safe") forcedDanger_ = false;   // 안전 복귀 시 강제상태 해제
    }

    // 강제 전환: 자연 판단이 warning이어도 danger로 승격 (경고 원인에 맞춰)
    if (forcedDanger_ && o.j.state == "warning") {
        o.j.state = "danger";
        if      (o.j.cause == Cause::SmokeVisual) o.j.cause = Cause::SmokeConfirmed;  // 연기 경고 → 연기 위험
        else if (o.j.cause == Cause::FireVisual)  o.j.cause = Cause::FireConfirmed;   // 화재 경고 → 화재 위험
        else if (o.j.cause == Cause::FlameSensor) o.j.cause = Cause::FireConfirmed;   // 불꽃센서 경고 → 화재 위험
        else                                      o.j.cause = Cause::FireConfirmed;   // 기본
    }

    if (o.j.state != prevState_)   // 판단 상태 전이 로그 (강제전환 반영된 최종 상태)
        std::cout << "[판단] " << prevState_ << " → " << o.j.state
                  << " (" << (o.j.cause.empty() ? "정상" : o.j.cause) << ")\n";

    // 엣지 트리거: 위험 "진입" 또는 위험 중 "원인 변경" 순간에만 발사
    // (가스로 팬 최대 배출 중 → 불 붙음 → 팬 차단으로 뒤집어야 함)
    if (o.j.state == "danger" && (prevState_ != "danger" || o.j.cause != prevCause_)) {
        if (incidentId_ == 0) {                  // 경고 없이 바로 위험이면 여기서 발급 
            incidentId_      = ++incidentSeq_;
            incidentStartTs_ = nowTs;
        }
        wasDanger_ = true;
        o.dangerEntered = true;
    }
    else if (incidentId_ != 0 && o.j.state == "safe") {   // 사태 열려있는데 안전 복귀 = 종료 
        o.released   = true;
        o.wasDanger  = wasDanger_;
        o.durationMs = (nowTs - incidentStartTs_) * 1000;   // 초→ms
    }                                                                   

    o.incidentId = incidentId_;
    if (o.released) { incidentId_ = 0; wasDanger_ = false; }   // 사태 종료

    prevState_ = o.j.state;
    prevCause_ = o.j.cause;
    return o;
}