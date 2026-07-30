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
class MonitorPage;
class EventLogPage;
class GraphPage;
class ControlPage;
class HelpPage;
class ServerLink;

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

    void switchTab(int index);
    void switchZone(int index);
    void refreshZoneUi();
    // zone.state가 Warning으로 새로 바뀐 순간에만 호출됨. 어느 탭을 보고 있든 팝업이 뜬다.
    void showWarningAlert(const QString &zoneName, const QString &zoneId, const QString &cause);
    // 위험 배너 + 화면 가장자리 글로우 + 모니터링 탭 강조. zones 상태가 바뀌거나 탭 전환할 때마다 호출.
    void updateDangerIndicators();

    QStackedWidget *stack;
    QList<QPushButton *> tabButtons;
    QList<QPushButton *> zoneButtons;
    QList<Zone> zones;
    int currentZone = 0;

    // 어느 탭에 있든 항상 보이는 상단 종합상태 배지("● A구역 안전" 등).
    QLabel *topStatusLabel;

    QWidget *centralArea = nullptr;       // 루트 레이아웃을 담는 위젯 (setCentralWidget 대상)
    QPushButton *dangerBanner = nullptr;  // 위험 구역 있으면 상단에 표시, 클릭 시 해당 구역 모니터링으로 이동
    int dangerBannerZoneIndex = -1;
    DangerGlowOverlay *dangerGlow = nullptr; // 가장자리 은은한 빨간 글로우(숨쉬듯 펄스)

    MonitorPage *monitorPage;
    EventLogPage *eventLogPage;
    GraphPage *graphPage;
    ControlPage *controlPage;
    HelpPage *helpPage;

    ServerLink *serverLink;
    // cmdId -> 표시용 제목("환기팬 가동" 등). control_ack/타임아웃 왔을 때 로그 문구에 씀.
    QMap<QString, QString> pendingControlTitles;

    // actuator_status로 받은 마지막 값(-1=아직 모름). 수동제어 클릭 시 낙관적으로도 갱신해서
    // 모니터링 탭 종합상태에도 즉시 반영한다.
    int currentFan = -1;
    int currentValve = -1;
    int currentSiren = -1;
};
#endif // MAINWINDOW_H
