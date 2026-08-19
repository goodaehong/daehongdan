#include "alarm_state.h"
#include <iostream>

// ── Qt 요청 접수. 거절하지 않는다 (켜는 방향은 막지 않음) ──
void AlarmState::requestEmergency(bool on, const std::string& cause, const std::string& admin) {
    std::lock_guard<std::mutex> lk(reqMtx_);
    if (on) { reqTrigger_ = true; reqCause_ = cause; }
    else      reqClear_ = true;
    reqAdmin_ = admin;
}

AlarmOutcome AlarmState::update(const Judgement& in, long nowTs) {
    AlarmOutcome o{};
    o.j = in;
    o.naturalState = in.state;   // 래치·수동 전환 적용 전 원본
    o.warnRemain = -1;           // -1 = JSON에 미포함 (warning 아닐 때)

    // ── 수신 스레드가 넣어둔 요청 꺼내기 ──
    bool trig = false, clr = false;
    std::string reqCause, reqAdmin;
    {
        std::lock_guard<std::mutex> lk(reqMtx_);
        trig = reqTrigger_; clr = reqClear_;
        reqCause = reqCause_; reqAdmin = reqAdmin_;
        reqTrigger_ = reqClear_ = false;
    }

    // 해제를 먼저 본다. 이 tick에서는 래치를 다시 걸지 않는다
    // (같은 tick에 풀고 걸면 사태 번호와 액추에이터가 꼬임 — 재발이면 다음 tick에 새 사태로)
    bool justCleared = false;
    if (clr && latched_) {
        latched_ = false; manual_ = false; forcedDanger_ = false;
        manualAdmin_.clear();
        o.emergCleared = true;
        justCleared = true;
        std::cout << "[비상] 해제 — 확인자: " << reqAdmin << "\n";
    }
    if (trig) {
        if (latched_) {           // 이미 위험 = 대응 재실행. 원인·발령자는 그대로 둔다
            o.emergEntered = true;
            o.emergReapply = true;   
            std::cout << "[비상] 대응 재실행 — " << reqAdmin << "\n";
        } else {
            manual_ = true; manualCause_ = reqCause; manualAdmin_ = reqAdmin;
            o.emergEntered = true;
            std::cout << "[비상] 전환 — 원인: " << reqCause << " (" << reqAdmin << ")\n";
        }
    }

    // ── 경고 무응답 타이머: warning 지속 중 관리자 미확인 시 위험 강제 전환 ──
    if (o.j.state == "warning") {
        if (warnStartTs_ < 0) {          // warning 진입 순간
            warnStartTs_  = nowTs;
            ack_          = false;       // 새 경고 시작 → 이전 ack 무효화
            ackLogged_    = false;
            forcedDanger_ = false;
            if (incidentId_ == 0) {              // 새 사태일 때만 기록·로그      
                incidentId_      = ++incidentSeq_;
                incidentStartTs_ = nowTs;
                o.warnEntered = true;
                std::cout << "[경고] " << o.j.cause << " 발생 → 관리자 알림 ("
                          << WARN_TIMEOUT << "초 대기)\n";
            }                                                               
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
        if (warnStartTs_ >= 0 && o.j.state == "safe" && !forcedDanger_ && !latched_)  // 래치 중엔 해제 로그 금지
            std::cout << "[경고] 해제됨 (감지 사라짐)\n";
        // warnStartTs_는 여기서 리셋하지 않는다. 깜빡일 때마다 10초가 초기화되면 
        // 실제 화재도 위험으로 못 올라간다. 사태가 종료될 때 같이 리셋한다
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

    // ── 수동 발령: 자연 판정이 위험이 아니어도 위험으로 만든다 ──
    // 자연 판정이 이미 위험이면 그쪽 원인이 더 정확하므로 건드리지 않는다
    if (manual_ && o.j.state != "danger") {
        o.j.state = "danger";
        o.j.cause = manualCause_;
    }

    // ── 래치: 위험에 닿으면 걸리고, 수동 해제 전까지 안 풀린다 ──
    if (o.j.state == "danger" && !justCleared) {
        latched_ = true; latchCause_ = o.j.cause;
        hazardEndTs_ = 0;                       // 아직 위험 지속 중
    } else if (latched_) {
        if (hazardEndTs_ == 0) {                // 자연 판정이 막 안전으로 돌아온 순간
            hazardEndTs_ = nowTs;
            std::cout << "[비상] 수치 정상 복귀 — 관리자 확인 대기\n";
        }
        o.j.state = "danger";                   // 래치 유지
        o.j.cause = latchCause_;
    }
    o.manual = manual_;
    o.admin  = manualAdmin_;

    if (o.j.state != prevState_)   // 판단 상태 전이 로그 (최종 상태 기준)
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

    // 안전이 잠깐 스쳐도 사태를 닫지 않는다. 감지가 깜빡이면 한 사건이 여러 건으로 쪼개짐 
    if (o.j.state == "safe") { if (safeSinceTs_ == 0) safeSinceTs_ = nowTs; }
    else                       safeSinceTs_ = 0;
    bool safeHeld = (safeSinceTs_ != 0 && nowTs - safeSinceTs_ >= RELEASE_HOLD);

    // 사태 종료: 위험까지 갔으면 수동 해제로만, 경고만이었으면 안전이 지속될 때
    if (o.emergCleared || (incidentId_ != 0 && !latched_ && safeHeld)) {     
        o.released   = true;
        o.wasDanger  = wasDanger_;
        // 관리자 반응 시간이 통계에 섞이지 않게, 수치가 정상으로 돌아온 시각까지만 센다
        long endTs   = hazardEndTs_ ? hazardEndTs_ : nowTs;
        o.durationMs = (endTs - incidentStartTs_) * 1000;   // 초→ms
    }

    o.incidentId = incidentId_;
    if (o.released) { incidentId_ = 0; wasDanger_ = false; hazardEndTs_ = 0;
                      warnStartTs_ = -1; safeSinceTs_ = 0; } 

    // 해제한 tick은 다음 위험이 새 사태로 잡히도록 전이 기준을 초기화한다
    prevState_ = o.emergCleared ? "safe" : o.j.state;
    prevCause_ = o.emergCleared ? ""     : o.j.cause;
    return o;
}