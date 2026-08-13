#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QMap>
#include "ZoneTypes.h"

class QStackedWidget;
class QPushButton;
class QLabel;
class DangerGlowOverlay;
class WarningAlertDialog;
class MonitorPage;
class EventLogPage;
class GraphPage;
class HelpPage;
class FloorMapPage;
class ServerLink;
class QTimer;
class QVBoxLayout;
class QResizeEvent;

// 상단 메뉴(구역 토글 + 페이지 탭)와 페이지 전환만 담당하는 셸.
// 실제 화면 내용은 pages/*Page 클래스가 각자 소유한다.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

signals:
    void loggedOut();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void setZoneState(int zoneIndex, ZoneState state);

private:
    QWidget *createTopBar();
    QWidget *createSubTabBar();
    QWidget *createDangerBanner();
    QWidget *createWarningAlertArea();
    void positionWarningAlert(bool animate);

    void switchTab(int index);
    void switchZone(int index);
    void refreshZoneUi();
    // zone.state가 Warning으로 새로 바뀐 순간에만 호출됨. 어느 탭을 보고 있든 팝업이 뜬다.
    void showWarningAlert(const QString &zoneName, const QString &zoneId, const QString &cause, int warnRemain);
    // 위험 배너 + 화면 가장자리 글로우 + 모니터링 탭 강조. zones 상태가 바뀌거나 탭 전환할 때마다 호출.
    void updateDangerIndicators();
    // 소켓 연결 여부 + sensor 메시지 수신 흐름(5초 이상 끊기면 정지로 판단)을 합쳐 connBadge 3단계로 표시.
    // (emergency-mode #19 — TCP는 붙어있어도 서버 내부가 멎으면 "연결됨"으로 잘못 보이는 문제 보완)
    void refreshConnBadge();
    // 평면도가 아직 등록 안 됐으면 "평면도" 탭에 배지 표시(모니터링 탭의 위험 🔴 점과 같은 패턴).
    // 지금 그 탭을 보고 있을 때는 안 띄운다 — 이미 보고 있는 화면을 또 알릴 필요는 없어서.
    void refreshFloorMapTabBadge();

    QStackedWidget *stack;
    QList<QPushButton *> tabButtons;
    QList<QPushButton *> zoneButtons;
    QList<Zone> zones;
    int currentZone = 0;

    // 어느 탭에 있든 항상 보이는 상단 종합상태 배지("● A구역 안전" 등).
    QLabel *topStatusLabel;
    // 서버 소켓 실제 연결 상태를 그대로 반영하는 배지 (ServerLink::connectionStateChanged로 갱신).
    QLabel *connBadge;
    // TCP는 붙어있어도 서버 내부(센서 스레드)가 멎으면 sensor 메시지가 끊긴다 — 소켓 연결 여부만으론
    // 이 상태를 못 잡으므로 별도로 감시한다 (emergency-mode #19, 서버 추가 작업 없음, 순수 Qt 감시).
    QTimer *sensorWatchdogTimer = nullptr;
    QDateTime lastSensorMsgAt;
    bool socketConnected = false;

    QWidget *centralArea = nullptr;       // 루트 레이아웃을 담는 위젯 (setCentralWidget 대상)
    QPushButton *dangerBanner = nullptr;  // 위험 구역 있으면 상단에 표시, 클릭 시 해당 구역 모니터링으로 이동
    int dangerBannerZoneIndex = -1;
    DangerGlowOverlay *dangerGlow = nullptr; // 가장자리 은은한 빨간 글로우(숨쉬듯 펄스)

    QWidget *warningAlertArea = nullptr;
    QVBoxLayout *warningAlertLayout = nullptr;

    MonitorPage *monitorPage;
    EventLogPage *eventLogPage;
    GraphPage *graphPage;
    HelpPage *helpPage;
    FloorMapPage *floorMapPage;
    // 평면도 탭에 아직 지도가 등록 안 됐을 때 탭 배지("🔶 평면도")를 갱신하는 데 쓰는 인덱스.
    int floorMapTabIndex = -1;

    ServerLink *serverLink;
    // cmdId -> 표시용 제목("환기팬 가동" 등). control_ack/타임아웃 왔을 때 로그 문구에 씀.
    QMap<QString, QString> pendingControlTitles;
    // 그래프 탭이 마지막으로 보낸 sensor_log 조회 reqId. event_log 초기 프리필과 target이 같아서
    // reqId로 구분해야 응답을 엉뚱한 쪽(그래프 vs 실시간 미니그래프 프리필)으로 안 보낸다.
    QString pendingGraphReqId;

    // actuator_status로 받은 마지막 값(-1=아직 모름). 수동제어 클릭 시 낙관적으로도 갱신해서
    // 모니터링 탭 종합상태에도 즉시 반영한다.
    int currentFan = -1;
    int currentValve = -1;
    int currentSiren = -1;

    // zoneId -> 현재 표시 중인 상단 경고 배너. sensor 메시지의 warnRemain을 실시간으로 반영하거나,
    // 경고 상태를 벗어나면(안전 복귀/위험 전환) 자동으로 닫기 위해 추적한다.
    QMap<QString, WarningAlertDialog *> activeWarningDialogs;

    // DEMO 버튼으로 띄운 경고는 서버가 없어 warnRemain을 받을 수 없으므로,
    // 여기서만 로컬로 1초마다 카운트다운하고 0이 되면 위험으로 자동 전환해 실제 흐름을 재현한다.
    void startDemoWarningCountdown(int zoneIndex, const QString &zoneId, int seconds);
    void stopDemoWarningCountdown();
    QTimer *demoWarningTimer = nullptr;
    QString demoWarningZoneId;
    int demoWarningZoneIndex = -1;
    int demoWarningRemain = 0;
};
#endif // MAINWINDOW_H
