#include "ZoneTypes.h"
#include <QStringList>

QString colorForState(ZoneState state)
{
    switch (state) {
    case ZoneState::Safe: return "#34d399";
    case ZoneState::Warning: return "#fbbf24";
    case ZoneState::Danger: return "#f87171";
    }
    return "#34d399";
}

QString textForState(ZoneState state)
{
    switch (state) {
    case ZoneState::Safe: return "안전";
    case ZoneState::Warning: return "경고";
    case ZoneState::Danger: return "위험";
    }
    return "안전";
}

ZoneState zoneStateFromString(const QString &state)
{
    if (state == "warning")
        return ZoneState::Warning;
    if (state == "danger")
        return ZoneState::Danger;
    return ZoneState::Safe;
}

QString causeText(const QString &causeCode)
{
    // server/judgement.h Cause 네임스페이스 값과 1:1 (판단 매트릭스 1~8행 순서로 나열)
    if (causeCode == "smoke_visual") return "연기 감지 주의";        // 1행 (warning)
    if (causeCode == "fire_visual") return "화재 감지 주의";         // 2행 (warning)
    if (causeCode == "flame_sensor") return "화염 감지";             // 3행 (warning)
    if (causeCode == "smoke_sensor") return "연기 농도 위험";        // 4행 (danger)
    if (causeCode == "smoke_confirmed") return "화재+연기 발생";     // 5행 (danger)
    if (causeCode == "fire_confirmed") return "화재 감지 확정";      // 6행 (danger)
    if (causeCode == "gas") return "가스 누출 발생";                 // 7행 (danger)
    if (causeCode == "gas_fire_flame") return "가스+화재 발생";      // 8행 (danger)
    if (causeCode == "gas_fire_smoke") return "가스+화재 발생";      // 8행 (danger)
    if (causeCode == "gas_fire_smokesensor") return "가스+화재 발생"; // 8행 (danger)
    return QString();
}

QString channelTargetName(int channel1Based)
{
    static const QStringList kTargets = { "화재감지", "가스배관", "환기팬", "사이렌" };
    if (channel1Based >= 1 && channel1Based <= kTargets.size())
        return kTargets[channel1Based - 1];
    return QString("Ch.%1").arg(channel1Based);
}
