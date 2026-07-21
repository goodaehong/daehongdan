#include "ZoneTypes.h"

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
