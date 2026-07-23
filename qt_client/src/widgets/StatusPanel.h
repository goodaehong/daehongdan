#ifndef STATUSPANEL_H
#define STATUSPANEL_H

#include <QWidget>
#include <QList>
#include "../core/ZoneTypes.h"

class QLabel;
class QPushButton;

// 좌측 구역 종합상태 카드: 히어로 글로우 서클 + 센서 수치 + DEMO 상태 시뮬레이션 버튼.
class StatusPanel : public QWidget
{
    Q_OBJECT

public:
    explicit StatusPanel(QWidget *parent = nullptr);

    void updateZone(const Zone &zone);
    void setCameraStatus(const QString &text, const QString &color);
    // 서버 actuator_status 반영. fan: 0=OFF/1=약/2=중/3=강, valve: 0=잠금/1=개방, siren: 0=OFF/1=ON (그 외는 "확인 중")
    void setActuatorStatus(int fan, int valve, int siren);

signals:
    void demoStateRequested(ZoneState state);

private:
    QLabel *heroTitleLabel;
    QLabel *heroCircle;
    QLabel *heroStateLabel;
    QLabel *tempValueLabel;
    QLabel *humidityValueLabel;
    QLabel *gasValueLabel;
    QLabel *smokeValueLabel;
    QLabel *cameraStatusValueLabel;
    QLabel *fanValueLabel;
    QLabel *valveValueLabel;
    QLabel *sirenValueLabel;
    QList<QPushButton *> demoStateButtons;
};

#endif // STATUSPANEL_H
