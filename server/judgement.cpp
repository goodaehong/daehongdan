#include "judgement.h"

// 판단 임계값 — 클린에어 실측(가스 0.0 / 연기 27) 기준 임시치. 위험 상황 실측 후 재조정
constexpr float GAS_THRESHOLD   = 50.0f;    // MQ-9 가스   (클린에어 0.0)
constexpr float SMOKE_THRESHOLD = 100.0f;   // MQ-2 연기   (클린에어 27 — 마진 확보)
constexpr float FLAME_THRESHOLD = 400.0f;   // DFR0076 불꽃센서(AO, 환산 없음)        

// cause 코드값 → Qt 표시용 센서 조합 문구 (트리거 센서 칸)
std::string causeToCombo(const std::string& cause) {
    if (cause == Cause::FireConfirmed)  return "화재 영상 감지 + 불꽃 센서";
    if (cause == Cause::SmokeConfirmed) return "연기 영상 감지 + MQ-2(연기 센서)";
    if (cause == Cause::SmokeSensor)    return "MQ-2(연기 센서)";
    if (cause == Cause::Gas)            return "MQ-9(가스 센서)";
    if (cause == Cause::FlameSensor)    return "불꽃 센서";
    if (cause == Cause::FireVisual)     return "화재 영상 감지";
    if (cause == Cause::SmokeVisual)    return "연기 영상 감지";
    // 가스+화재 = 가스 문구 + 화재 종류 문구 조합 (중복 제거 위해 재사용)
    if (cause == Cause::GasFireFlame)       return causeToCombo(Cause::Gas) + " + " + causeToCombo(Cause::FireConfirmed);
    if (cause == Cause::GasFireSmoke)       return causeToCombo(Cause::Gas) + " + " + causeToCombo(Cause::SmokeConfirmed);
    if (cause == Cause::GasFireSmokeSensor) return causeToCombo(Cause::Gas) + " + " + causeToCombo(Cause::SmokeSensor);
    return "-";
}

// 판단 매트릭스 2차. 영상 감지(O/X) + 센서 값 → 종합 판정
Judgement judgeState(bool camFire, bool camSmoke, const SensorReading& s) {
    bool gasHigh   = s.gasPpm   > GAS_THRESHOLD;    // MQ-9
    bool smokeHigh = s.smokePpm > SMOKE_THRESHOLD;  // MQ-2
    bool flameHigh = s.flameVal > FLAME_THRESHOLD;  // DFR0076 (역방향이면 < 로)

    // ── danger (위험) ──
    if (gasHigh && camFire && flameHigh)   return {"danger", Cause::GasFireFlame};        // 8행 가스 + 화재(화재감지+불꽃센서)
    if (gasHigh && camSmoke && smokeHigh)  return {"danger", Cause::GasFireSmoke};        // 8행 가스 + 화재(연기감지+연기센서)
    if (gasHigh && smokeHigh)              return {"danger", Cause::GasFireSmokeSensor};  // 8행 가스 + 화재(연기센서)
    if (camFire && flameHigh)    return {"danger", Cause::FireConfirmed};   // 6행
    if (camSmoke && smokeHigh)   return {"danger", Cause::SmokeConfirmed};  // 5행
    if (smokeHigh)               return {"danger", Cause::SmokeSensor};     // 4행
    if (gasHigh)                 return {"danger", Cause::Gas};             // 7행

    // ── warning (경고) ──
    if (flameHigh)               return {"warning", Cause::FlameSensor};    // 3행
    if (camFire)                 return {"warning", Cause::FireVisual};     // 2행
    if (camSmoke)                return {"warning", Cause::SmokeVisual};    // 1행

    return {"safe", Cause::None};
}

// 원인별 대응. 가스 단독만 배기, 화재 계열은 전부 산소 차단
Response decideResponse(const std::string& cause) {
    if (cause == Cause::Gas)
        return {3, 0, 1};   // 팬 최대(배기) + 밸브 차단 + 사이렌
    return {0, 0, 1};       // 화재 계열 — 팬 차단(산소) + 밸브 차단 + 사이렌
}

// 위험 해제 시 복귀
Response responseForSafe() {
    return {1, 1, 0};       // 팬 약 + 밸브 개방 + 사이렌 OFF
}