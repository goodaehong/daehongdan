#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QList>
#include <QMap>
#include "ZoneTypes.h"

class QStackedWidget;
class QPushButton;
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

    void switchTab(int index);
    void switchZone(int index);
    void refreshZoneUi();

    QStackedWidget *stack;
    QList<QPushButton *> tabButtons;
    QList<QPushButton *> zoneButtons;
    QList<Zone> zones;
    int currentZone = 0;

    MonitorPage *monitorPage;
    EventLogPage *eventLogPage;
    GraphPage *graphPage;
    ControlPage *controlPage;
    HelpPage *helpPage;

    ServerLink *serverLink;
    // cmdId -> 표시용 제목("환기팬 가동" 등). control_ack/타임아웃 왔을 때 로그 문구에 씀.
    QMap<QString, QString> pendingControlTitles;
};
#endif // MAINWINDOW_H
