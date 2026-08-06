#pragma once
#include <string>
#include "sensors/sensor_reader.h"

// cause 코드값. JSON·DB에 그대로 나감 (명세서 cause 표와 1:1)
// 문자열 직접 쓰지 말고 이걸 쓸 것 — 오타가 컴파일 에러로 잡힘
namespace Cause {
    inline constexpr const char* None               = "";
    inline constexpr const char* GasFireFlame       = "gas_fire_flame";        // 8행
    inline constexpr const char* GasFireSmoke       = "gas_fire_smoke";        // 8행
    inline constexpr const char* GasFireSmokeSensor = "gas_fire_smokesensor";  // 8행
    inline constexpr const char* FireConfirmed      = "fire_confirmed";        // 6행
    inline constexpr const char* SmokeConfirmed     = "smoke_confirmed";       // 5행
    inline constexpr const char* SmokeSensor        = "smoke_sensor";          // 4행
    inline constexpr const char* Gas                = "gas";                   // 7행
    inline constexpr const char* FlameSensor        = "flame_sensor";          // 3행
    inline constexpr const char* FireVisual         = "fire_visual";           // 2행
    inline constexpr const char* SmokeVisual        = "smoke_visual";          // 1행
}

struct Judgement {
    std::string state;   // "safe"/"warning"/"danger" → Qt로 전송
    std::string cause;   // 대응 선택용
};

// 자동 대응 내용. 값만 정하고 실행은 actuator_control이
struct Response {
    int fan;     // 0=OFF, 1~3=약/중/강
    int valve;   // 0=닫힘, 1=열림
    int siren;   // 0=OFF, 1=ON
};

// 판단 매트릭스 2차. 영상 감지(O/X) + 센서 값 → 종합 판정
Judgement judgeState(bool camFire, bool camSmoke, const SensorReading& s);

// 원인별 대응. 가스=팬 최대(배기) / 화재계열=팬 차단(산소 차단)
Response decideResponse(const std::string& cause);

// 위험 해제 시 복귀 (사이렌 OFF + 밸브 개방 + 팬 약)
Response responseForSafe();

// cause 코드값 → DB sensor_combo 문구 (트리거 센서 칸)
std::string causeToCombo(const std::string& cause);

// 가스 농도 → 전광판 표시 단계 (0=정상, 1=주의, 2=위험)
int gasLevel(float gasPpm);