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
class QGraphicsDropShadowEffect;

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
    // 서버 sensor 메시지의 visionOk(채널별 서버 영상 감지 생존 여부) 반영 (emergency-mode #14).
    // Qt 자체 영상 수신(setCameraChannelStatus)과 조합해 4색으로 표시: 정상/화면만끊김/감지만끊김(최위험)/전부끊김.
    void setCameraVisionStatus(bool ch1, bool ch2, bool ch3, bool ch4);
    // 서버 actuator_status 반영. fan: 0=OFF/1=약/2=중/3=강, valve: 0=잠금/1=개방, siren: 0=OFF/1=ON (그 외는 "확인 중")
    // link: "ok"/"down"(빈 문자열=미상, STM보드 공통이라 fan/valve/siren 개별 구분 불가).
    // fanSrc/valveSrc/sirenSrc: 각각 "auto"/"manual"(빈 문자열=미상). 위험 시 셋 다 자동으로 바뀐 뒤
    // 관리자가 하나만 수동 조작하는 경우가 있어서 액추에이터별로 따로 옴.
    // 읽기전용 표시뿐 아니라 수동 제어 버튼의 활성 하이라이트도 같이 갱신한다.
    // targetFan/targetValve/targetSiren: 서버 목표값. 실제값과 다르면 장치별 ⚠ 표시 (emergency-mode #15).
    // linkReason: STM 링크 끊김 사유 (emergency-mode #16).
    void setActuatorStatus(int fan, int valve, int siren, const QString &link,
                            const QString &fanSrc, const QString &valveSrc, const QString &sirenSrc,
                            int targetFan = -1, int targetValve = -1, int targetSiren = -1,
                            const QString &linkReason = QString());
    // 수동 제어 명령의 대기중/성공/실패/타임아웃 결과를 잠깐 보여준다(몇 초 후 자동으로 사라짐).
    void showCommandStatus(const QString &text, const QString &color);
    // 특정 액추에이터(fan/valve/siren) 한 줄만 "처리 중.../응답 없음" 등으로 잠깐 덮어쓴다.
    // 다음 실제 actuator_status가 오면(성공 시 명령 직후 자동으로 옴) 정상 값으로 되돌아간다.
    void setActuatorRowStatus(const QString &target, const QString &text, const QString &color);

signals:
    void demoStateRequested(ZoneState state);
    // target: "fan"/"valve"/"siren". action: off/low/mid/high, close/open, on/off. title은 로그/상태 표시용 문구.
    void controlActionRequested(const QString &target, const QString &action, const QString &title);
    // 정상/경고 상태에서 신규 전환, 또는 위험·대응실패 상태에서 재실행 요청. cause는 judgement.h Cause 값.
    void emergencyTriggerRequested(const QString &cause);
    // 위험 상태에서 해제 요청. checklist는 체크리스트 모달의 "현장 확인" 항목 키 목록 (emergency-mode #10~11).
    void emergencyClearRequested(const QString &admin, const QStringList &checklist);

private:
    void updateElapsedLabel();
    void refreshCameraHeader();
    QString pillStyle(const QString &color, bool filled) const;
    void updateControlButtonStyles(QVector<QPushButton *> &buttons, int activeIndex);
    bool showConfirmDialog(const QString &actionName);
    // 대피 모드는 전 구역에 영향을 주고 되돌리기 어려워 일반 확인 1번으로는 부족 -> 2단계 확인.
    bool showEvacuationConfirmDialog();
    // 원인 선택 모달(정상 상태 전용, emergency-mode #8). 선택된 cause를 outCause에 채우고 true, 취소하면 false.
    bool showEmergencyCauseDialog(QString &outCause);
    // 해제 체크리스트 모달(emergency-mode #10~11). 시스템 확인 3(서버 판정, 캐시값) + 현장 확인 3(원인별) +
    // 확인자 이름. 전부 체크+이름 입력해야 [해제 확정] 활성화. 확정 시 outAdmin/outChecklist 채우고 true.
    bool showEmergencyClearDialog(QString &outAdmin, QStringList &outChecklist);
    void updateModeLabel(QLabel *label, const QString &source);
    // 상태별 버튼 활성/비활성/라벨 갱신 (emergency-mode #6~7). updateZone()에서 매번 호출.
    void updateEmergencyButtons(ZoneState state, bool responseOk);
    // 해제 버튼의 활성/비활성 스타일만 담당(전환 버튼과 색 규칙이 대칭이라 분리).
    void setClearButtonActive(bool active);
    // "⟳ 대응 재실행" 텍스트에 lastLinkReason이 있으면 괄호로 덧붙인다.
    void updateRetryButtonText();
    // 대응 실패(주황) 상태에서만 emergencyBlinkTimer가 호출 — 배경색만 두 톤으로 번갈아 칠한다.
    void applyRetryButtonBlinkStyle();
    // channelConnected/channelVisionOk 조합해서 채널 점 4색 갱신 (emergency-mode #14).
    void refreshChannelColor(int index);

    QLabel *heroTitleLabel;
    QLabel *heroCircle;
    QGraphicsDropShadowEffect *heroGlow;
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
    // 서버 sensor 메시지의 visionOk. 최초 메시지 오기 전엔 알 수 없으니 true(정상) 기본값으로 시작
    // (구버전 서버에서 필드 자체가 없는 경우와 동일한 취급 — 없다고 오류 표시하면 안 됨).
    bool channelVisionOk[4] = { true, true, true, true };

    QLabel *actuatorLinkLabel; // "● 연결됨"/"● 연결 끊김"/"확인 중" (STM보드(1) 공통)
    QLabel *actuatorLinkInfoIcon; // 위 상태에 마우스 올리면 이유를 툴팁으로 보여주는 "ⓘ" 아이콘
    QString lastLinkReason;   // STM 링크 끊김 사유 캐시 — "⟳ 대응 재실행" 버튼 문구에도 재사용
    // 위험 감지 센서(가스/불꽃/연기, ADS1115)와 환경(온습도, DHT22)은 서로 다른 센서라 배지도
    // 카드별로 분리한다 — 예전엔 하나로 합쳐서 온습도 문제가 엉뚱한 카드(위험 감지 센서)에 떴었다.
    QLabel *sensorLinkBadge;  // "위험 감지 센서" 카드 헤더 "🟢 연결됨"/"🔴 센서 오류"
    QLabel *envLinkBadge;     // "환경" 카드 헤더 "🟢 연결됨"/"🟡 온습도 불안정"
    // "자동(평상시)" vs "자동(위험 대응)" 구분에 쓰는 구역 상태 캐시.
    ZoneState lastKnownZoneState = ZoneState::Safe;
    QString lastKnownCause;   // 위험 모드 전환 시 경고/재실행 상태에서 원인 재사용 (emergency-mode #9)
    // 해제 체크리스트 "시스템 확인" 3항목 캐시 (emergency-mode #10). 서버 clearCheck 값 그대로.
    bool lastClearSensor = false, lastClearVision = false, lastClearActuator = false;
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

    QPushButton *emergencyTriggerButton = nullptr;
    QPushButton *emergencyClearButton = nullptr;
    // 위험·대응실패 상태에서 전환 버튼을 주황 두 톤으로 번갈아 칠해 "지금 확인해야 할 것"임을 강조.
    QTimer *emergencyBlinkTimer = nullptr;
    bool emergencyBlinkOn = true;

    QList<QPushButton *> demoStateButtons;
};

#endif // STATUSPANEL_H
