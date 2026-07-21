#ifndef ZONETYPES_H
#define ZONETYPES_H

#include <QString>

enum class ZoneState { Safe, Warning, Danger };

struct Zone {
    QString name;
    ZoneState state = ZoneState::Safe;
    double temp = 0;
    double humidity = 0;
    double gasPpm = 0;
    double smokePpm = 0;
    // true면 라즈베리파이 계약②(sensor) 실측값 사용, false면 DEMO 시뮬레이션 값 사용.
    bool hasLiveSensorData = false;
};

QString colorForState(ZoneState state);
QString textForState(ZoneState state);
// 계약②의 state 문자열("safe"/"warning"/"danger") -> ZoneState 변환. 알 수 없으면 Safe.
ZoneState zoneStateFromString(const QString &state);

#endif // ZONETYPES_H
