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
// 위험 배너용 원인 코드 -> 한글 문구. 서버가 sensor 메시지에 cause 필드를 추가하면 사용.
// gas=가스 누출 / flame=화염 감지 / smoke_fire=화재(연기) / fire_gas=가스+화재 / smoke_watch=연기 감지 주의
QString causeText(const QString &causeCode);

#endif // ZONETYPES_H
