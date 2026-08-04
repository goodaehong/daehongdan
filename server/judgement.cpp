#include "judgement.h"

// 판단 임계값 (mock 기준, 실센서 오면 유나님 보정값으로)
constexpr float GAS_WARN_THRESHOLD = 200.0f; // MQ-9 주의
constexpr float GAS_DANGER_THRESHOLD = 2000.0f;   // MQ-9 위험 (200 (CO 기준) -> 2000 (LPG 기준) )
constexpr float SMOKE_THRESHOLD = 150.0f;   // MQ-2 연기센서
constexpr float FLAME_THRESHOLD = 1.0f;   // DFR0076 불꽃센서(AO) (400 -> 1.0V 보정)
// 실측 기준(2026-08-04): 평시 0.08~0.15V, 화염 노출 시 0.7~4.8V

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

// 가스 농도 → 전광판 표시 단계 (0=정상, 1=주의, 2=위험)     
// 주의(200~2000)는 표시 전용 — 판단 매트릭스는 위험만 씀
int gasLevel(float gasPpm) {
    if (gasPpm > GAS_DANGER_THRESHOLD) return 2;
    if (gasPpm > GAS_WARN_THRESHOLD)   return 1;
    return 0;
}          

// 판단 매트릭스 2차. 영상 감지(O/X) + 센서 값 → 종합 판정
Judgement judgeState(bool camFire, bool camSmoke, const SensorReading& s) {
    bool gasHigh   = s.gasPpm   > GAS_DANGER_THRESHOLD;    // MQ-9 (전광판 주황 표시는 GAS_WARN_THRESHOLD로 별도 처리)
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