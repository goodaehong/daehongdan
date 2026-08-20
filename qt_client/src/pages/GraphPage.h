#ifndef GRAPHPAGE_H
#define GRAPHPAGE_H

#include <QWidget>
#include <QVector>
#include <QDate>
#include "../core/ZoneTypes.h"

class QLabel;
class QPushButton;
class GasGraphWidget;
class QJsonArray;
class QTimer;

// 그래프 화면: 가스농도/연기농도 추이 + 기간·날짜 선택 UI.
// 기간/날짜 버튼을 누르면 sensorLogRequested를 emit하고, MainWindow가 ServerLink::sendQuery("sensor_log",...)로
// 요청한 뒤 응답을 loadSensorLogFromServer()로 넘겨준다 (EventLogPage와 동일한 패턴).
class GraphPage : public QWidget
{
    Q_OBJECT

public:
    explicit GraphPage(QWidget *parent = nullptr);
    void updateZone(const Zone &zone);

    // 서버 query_result(target="sensor_log") 응답 반영. rows 필드: t,gasAvg,gasMax,smokeAvg,smokeMax
    // (server/db/query_handler.cpp 참고). bucketSec==1(원본)이면 avg==max로 오므로 자동으로 선 하나만 그려진다.
    void loadSensorLogFromServer(const QJsonArray &rows);
    // 서버 query_result(target="event_log") 응답을 그대로 받아 경고/위험 시점만 골라 그래프 마커로
    // 쓴다. 이벤트로그 탭이 이미 주기적으로 조회하고 있어서 별도 조회 없이 그 응답을 같이 받는다.
    void setEventMarkersFromServer(const QJsonArray &rows);
    // 서버 연결 시(최초 접속 포함) 현재 선택된 기간/날짜/구역 기준으로 다시 요청한다.
    void requestCurrentPeriod();
    // 조회 실패 시 표시.
    void showQueryFailed(const QString &reason);

signals:
    // MainWindow가 받아서 ServerLink::sendQuery("sensor_log", {zone,from,to})를 호출한다.
    void sensorLogRequested(const QString &zone, qint64 from, qint64 to);

private:
    QWidget *createControlBar();
    void selectPeriod(int index);
    void updateNavButtons();
    void showDatePicker();
    // liveRefreshTimer를 실제 시계의 다음 "분" 경계에 맞춰 재시작한다 — 30초 같은 고정 주기 대신
    // 분이 바뀌는 순간 바로 갱신되도록.
    void scheduleNextMinuteTick();
    // 표본 시각(sampleTimes)과 사건 목록(eventTimes/eventDanger)을 합쳐 x비율 마커로 변환해 두 그래프에
    // 반영한다. 둘 중 아무거나 갱신될 때마다 호출 — 어느 쪽이 먼저 오든 순서에 안 눌리도록.
    void rebuildEventMarkers();

    QLabel *gasTitleLabel;
    QLabel *smokeTitleLabel;
    GasGraphWidget *gasGraph;
    GasGraphWidget *smokeGraph;

    QVector<QPushButton *> periodButtons; // 0=10분 1=1시간 2=6시간 3=하루
    QPushButton *dateButton;
    QPushButton *prevButton;
    QPushButton *todayButton;
    QPushButton *nextButton;
    QLabel *legendLabel;
    QLabel *noteLabel;
    int currentPeriodIndex = 0;
    QDate currentDate;
    QString currentZoneId; // "A"~"D". updateZone()에서 바뀔 때만 재조회한다(매초 오는 sensor 메시지마다 X).

    // 사건 마커용. 그래프 x축은 표본 인덱스 기준이라 "시각 -> x비율" 변환에 실제 표본 시각이 필요하다.
    QVector<qint64> sampleTimes;
    struct EventStamp { qint64 ts = 0; bool danger = false; QString label; };
    QVector<EventStamp> eventStamps;
    // 그래프를 보는 동안 시간이 흘러도 화면이 그 자리에 멈춰있지 않도록 주기적으로 다시 조회한다.
    QTimer *liveRefreshTimer = nullptr;
};

#endif // GRAPHPAGE_H
