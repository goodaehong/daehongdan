#ifndef ZONETYPES_H
#define ZONETYPES_H

#include <QString>
#include <QVector>
#include <QDateTime>

enum class ZoneState { Safe, Warning, Danger };

struct Zone {
    QString name;
    ZoneState state = ZoneState::Safe;
    double temp = 0;
    double humidity = 0;
    double gasPpm = 0;
    double smokePpm = 0;
    double flameVal = 0; // 불꽃센서(DFR0076) 전압(V). 평시 0.08~0.15V, 화염 시 0.7~4.8V (server/judgement.cpp FLAME_THRESHOLD=1.0V)
    // true면 라즈베리파이 계약②(sensor) 실측값 사용, false면 DEMO 시뮬레이션 값 사용.
    bool hasLiveSensorData = false;
    // 서버 sensor 메시지의 cause 코드 (server/judgement.h Cause 네임스페이스와 1:1). safe면 빈 문자열.
    QString cause;
    // 목표 대응(target)이 실제 액추에이터에 반영됐는가. 비상 모드 버튼 활성/비활성 판단에 씀.
    bool responseOk = true;
    // state가 마지막으로 바뀐 시각. StatusPanel의 "MM:SS 경과" 표시에 씀.
    QDateTime stateEnteredAt = QDateTime::currentDateTime();
    // 실시간 가스농도 추이 그래프용 최근 이력(최대 30개, 오래된 것부터).
    QVector<double> gasHistory;
    QVector<QString> gasHistoryLabels; // "HH:mm:ss", gasHistory와 1:1 대응
    // 불꽃센서 전압 추이(최대 30개). StatusPanel 추세(상승/하강/안정) 계산용.
    QVector<double> flameHistory;
    // 최근 연기 판정 이력(최대 8개, true=검지). StatusPanel "최근 N회 판정 · 감지 M회" 표시용.
    QVector<bool> smokeDetectHistory;
};

QString colorForState(ZoneState state);
QString textForState(ZoneState state);
// 계약②의 state 문자열("safe"/"warning"/"danger") -> ZoneState 변환. 알 수 없으면 Safe.
ZoneState zoneStateFromString(const QString &state);
// 위험/경고 배너·팝업용 원인 코드 -> 한글 문구 (이미 "발생/주의" 등 어미까지 포함된 완성 문구).
// server/judgement.h Cause 네임스페이스 값과 1:1 매핑 (판단 매트릭스 1~8행):
// gas_fire_flame/gas_fire_smoke/gas_fire_smokesensor=가스+화재 발생 / fire_confirmed=화재 감지 확정 /
// smoke_confirmed=화재+연기 발생 / smoke_sensor=연기 농도 위험 / gas=가스 누출 발생 /
// flame_sensor=화염 감지 / fire_visual=화재 감지 주의 / smoke_visual=연기 감지 주의
QString causeText(const QString &causeCode);
// 카메라 채널(1~4, 1-based)이 고정으로 감시하는 대상 이름. 범위 밖이면 "Ch.N" 반환.
QString channelTargetName(int channel1Based);

#endif // ZONETYPES_H
