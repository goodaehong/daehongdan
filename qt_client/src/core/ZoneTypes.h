#ifndef ZONETYPES_H
#define ZONETYPES_H

#include <QString>
#include <QVector>

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
    // 서버 sensor 메시지의 cause 코드("gas"/"flame"/"smoke_fire"/"fire_gas"/"smoke_watch"). 없으면 빈 문자열.
    QString cause;
    // 실시간 가스농도 추이 그래프용 최근 이력(최대 30개, 오래된 것부터).
    QVector<double> gasHistory;
    QVector<QString> gasHistoryLabels; // "HH:mm:ss", gasHistory와 1:1 대응
};

QString colorForState(ZoneState state);
QString textForState(ZoneState state);
// 계약②의 state 문자열("safe"/"warning"/"danger") -> ZoneState 변환. 알 수 없으면 Safe.
ZoneState zoneStateFromString(const QString &state);
// 위험/경고 배너·팝업용 원인 코드 -> 한글 문구 (이미 "발생/주의" 등 어미까지 포함된 완성 문구).
// gas=가스 누출 발생 / flame=화염 감지 / smoke_fire=화재+연기 발생 / fire_gas=가스+화재 발생 / smoke_watch=연기 감지 주의
QString causeText(const QString &causeCode);

#endif // ZONETYPES_H
