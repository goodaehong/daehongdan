#ifndef STATUSPANEL_H
#define STATUSPANEL_H

#include <QWidget>
#include <QList>
#include <QVector>
#include <QDateTime>
#include "../core/ZoneTypes.h"

class QLabel;
class QPushButton;
class QTimer;
class QFrame;
class GaugeBar;

// 좌측 구역 종합상태 카드: 히어로 글로우 서클 + 위험 감지 센서(가스/불꽃/연기) +
// 환경(온습도) + 카메라 채널 연결 현황 + 액추에이터 상태 + 수동 제어 + DEMO 상태 시뮬레이션 버튼.
// 수동 제어를 카메라 영상이 보이는 모니터링 화면 안에 두어, 조작 중에도 영상을 볼 수 있게 한다.
class StatusPanel : public QWidget
{
    Q_OBJECT

public:
    explicit StatusPanel(QWidget *parent = nullptr);

    void updateZone(const Zone &zone);
    // channel: 1~4(1-based). "카메라 채널" 목록의 연결 상태 점 색상에 반영.
    void setCameraChannelStatus(int channel, bool connected);
    // 서버 actuator_status 반영. fan: 0=OFF/1=약/2=중/3=강, valve: 0=잠금/1=개방, siren: 0=OFF/1=ON (그 외는 "확인 중")
    // link: "ok"/"down"(빈 문자열=미상, STM보드 공통이라 fan/valve/siren 개별 구분 불가).
    // fanSrc/valveSrc/sirenSrc: 각각 "auto"/"manual"(빈 문자열=미상). 위험 시 셋 다 자동으로 바뀐 뒤
    // 관리자가 하나만 수동 조작하는 경우가 있어서 액추에이터별로 따로 옴.
    // 읽기전용 표시뿐 아니라 수동 제어 버튼의 활성 하이라이트도 같이 갱신한다.
    void setActuatorStatus(int fan, int valve, int siren, const QString &link,
                            const QString &fanSrc, const QString &valveSrc, const QString &sirenSrc);
    // 수동 제어 명령의 대기중/성공/실패/타임아웃 결과를 잠깐 보여준다(몇 초 후 자동으로 사라짐).
    void showCommandStatus(const QString &text, const QString &color);
    // 특정 액추에이터(fan/valve/siren) 한 줄만 "처리 중.../응답 없음" 등으로 잠깐 덮어쓴다.
    // 다음 실제 actuator_status가 오면(성공 시 명령 직후 자동으로 옴) 정상 값으로 되돌아간다.
    void setActuatorRowStatus(const QString &target, const QString &text, const QString &color);
    // 서버 sensor 메시지의 evacuation 필드 반영. 전 구역 공통 상태라 zone 전환과 무관하게 항상 최신값.
    // 버튼 문구를 "대피 모드 발동"/"대피 모드 해제"로 전환한다.
    void setEvacuationActive(bool active);

signals:
    void demoStateRequested(ZoneState state);
    // target: "fan"/"valve"/"siren". action: off/low/mid/high, close/open, on/off. title은 로그/상태 표시용 문구.
    void controlActionRequested(const QString &target, const QString &action, const QString &title);
    // true=발동 요청, false=해제 요청. 대피 모드는 별도 메시지 타입(evacuation_trigger/clear)이라 분리.
    void evacuationActionRequested(bool activate);

private:
    void updateElapsedLabel();
    void refreshCameraHeader();
    QString pillStyle(const QString &color, bool filled) const;
    void updateControlButtonStyles(QVector<QPushButton *> &buttons, int activeIndex);
    bool showConfirmDialog(const QString &actionName);
    // 대피 모드는 전 구역에 영향을 주고 되돌리기 어려워 일반 확인 1번으로는 부족 -> 2단계 확인.
    bool showEvacuationConfirmDialog();
    void updateModeLabel(QLabel *label, const QString &source);

    QLabel *heroTitleLabel;
    QLabel *heroCircle;
    QLabel *heroStateLabel;
    QLabel *heroCauseLabel;
    QLabel *heroElapsedLabel;
    QTimer *elapsedTimer;
    QDateTime stateEnteredAt;

    QLabel *gasValueLabel;
    GaugeBar *gasGaugeBar;
    QLabel *gasTrendLabel;
    QLabel *flameValueLabel;
    GaugeBar *flameGaugeBar;
    QLabel *flameTrendLabel;
    QLabel *smokeValueLabel;
    GaugeBar *smokeGaugeBar;
    QLabel *smokeHistoryLabel;

    QLabel *tempValueLabel;
    QLabel *humidityValueLabel;

    QLabel *cameraHeaderLabel;
    QLabel *channelDotLabels[4];
    QFrame *channelFrames[4];
    bool channelConnected[4] = { false, false, false, false };

    QLabel *actuatorLinkLabel; // "● 연결됨"/"● 연결 끊김"/"확인 중" (STM보드(1) 공통)
    QLabel *actuatorLinkInfoIcon; // 위 상태에 마우스 올리면 이유를 툴팁으로 보여주는 "ⓘ" 아이콘
    // "자동(평상시)" vs "자동(위험 대응)" 구분에 쓰는 구역 상태 캐시.
    ZoneState lastKnownZoneState = ZoneState::Safe;
    QString lastFanSrc, lastValveSrc, lastSirenSrc; // 마지막 actuator_status의 소스 캐시("auto"/"manual"/"")

    QLabel *fanValueLabel;
    QLabel *valveValueLabel;
    QLabel *sirenValueLabel;
    // 액추에이터별 자동/수동 모드 배지.
    QLabel *fanModeLabel;
    QLabel *valveModeLabel;
    QLabel *sirenModeLabel;

    QVector<QPushButton *> fanCtrlButtons;   // [0]=OFF [1]=약 [2]=중 [3]=강
    QVector<QPushButton *> valveCtrlButtons; // [0]=잠금 [1]=개방
    QVector<QPushButton *> sirenCtrlButtons; // [0]=OFF [1]=ON
    QLabel *commandStatusLabel = nullptr;
    QTimer *statusClearTimer = nullptr;

    QPushButton *evacuationButton = nullptr;
    bool evacuationActive = false;

    QList<QPushButton *> demoStateButtons;
};

#endif // STATUSPANEL_H
