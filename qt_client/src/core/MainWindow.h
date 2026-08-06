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
class ServerLink;
class QTimer;

// 상단 메뉴(구역 토글 + 페이지 탭)와 페이지 전환만 담당하는 셸.
// 실제 화면 내용은 pages/*Page 클래스가 각자 소유한다.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

signals:
    void loggedOut();

private slots:
    void setZoneState(int zoneIndex, ZoneState state);

private:
    QWidget *createTopBar();
    QWidget *createSubTabBar();
    QWidget *createDangerBanner();
    QWidget *createEvacuationBanner();

    void switchTab(int index);
    void switchZone(int index);
    void refreshZoneUi();
    // zone.state가 Warning으로 새로 바뀐 순간에만 호출됨. 어느 탭을 보고 있든 팝업이 뜬다.
    void showWarningAlert(const QString &zoneName, const QString &zoneId, const QString &cause, int warnRemain);
    // 위험 배너 + 화면 가장자리 글로우 + 모니터링 탭 강조. zones 상태가 바뀌거나 탭 전환할 때마다 호출.
    void updateDangerIndicators();
    // 대피 모드는 zone과 무관한 전 구역 공통 상태라 어느 탭/구역을 보고 있든 항상 보이는 배너로 표시.
    void updateEvacuationBanner();

    QStackedWidget *stack;
    QList<QPushButton *> tabButtons;
    QList<QPushButton *> zoneButtons;
    QList<Zone> zones;
    int currentZone = 0;

    // 어느 탭에 있든 항상 보이는 상단 종합상태 배지("● A구역 안전" 등).
    QLabel *topStatusLabel;
    // 서버 소켓 실제 연결 상태를 그대로 반영하는 배지 (ServerLink::connectionStateChanged로 갱신).
    QLabel *connBadge;

    QWidget *centralArea = nullptr;       // 루트 레이아웃을 담는 위젯 (setCentralWidget 대상)
    QPushButton *dangerBanner = nullptr;  // 위험 구역 있으면 상단에 표시, 클릭 시 해당 구역 모니터링으로 이동
    int dangerBannerZoneIndex = -1;
    DangerGlowOverlay *dangerGlow = nullptr; // 가장자리 은은한 빨간 글로우(숨쉬듯 펄스)

    // 서버 sensor 메시지의 evacuation 필드로 갱신되는 전 구역 공통 상태(zone별 아님).
    QPushButton *evacuationBanner = nullptr;
    bool evacuationActive = false;

    MonitorPage *monitorPage;
    EventLogPage *eventLogPage;
    GraphPage *graphPage;
    HelpPage *helpPage;

    ServerLink *serverLink;
    // cmdId -> 표시용 제목("환기팬 가동" 등). control_ack/타임아웃 왔을 때 로그 문구에 씀.
    QMap<QString, QString> pendingControlTitles;

    // actuator_status로 받은 마지막 값(-1=아직 모름). 수동제어 클릭 시 낙관적으로도 갱신해서
    // 모니터링 탭 종합상태에도 즉시 반영한다.
    int currentFan = -1;
    int currentValve = -1;
    int currentSiren = -1;

    // zoneId -> 현재 열려있는 경고 팝업. sensor 메시지의 warnRemain을 실시간으로 반영하거나,
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
