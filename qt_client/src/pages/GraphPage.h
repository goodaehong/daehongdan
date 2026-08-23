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
    // 분이 바뀌는 순간 바로 갱신되도록. 매 tick마다 자동 전환 여부도 같이 확인한다.
    void scheduleNextMinuteTick();
    // 표본 시각(sampleTimes)과 사건 목록(eventTimes/eventDanger)을 합쳐 x비율 마커로 변환해 두 그래프에
    // 반영한다. 둘 중 아무거나 갱신될 때마다 호출 — 어느 쪽이 먼저 오든 순서에 안 눌리도록.
    void rebuildEventMarkers();

    // 기간 버튼 = "그 날짜 안에서의 구간 크기"(분 단위). "하루"는 하루 전체가 구간 하나.
    int periodMinutes(int index) const;
    // 하루를 그 구간 크기로 몇 조각 낼 수 있는지. "하루"는 항상 1.
    int segmentsPerDay(int index) const;
    // currentDate 기준 "지금 있어야 할 구간" 인덱스 — 오늘이면 지금 시각이 속한 구간,
    // 과거 날짜면 그 날의 마지막 구간(하루를 다 본 것으로 취급).
    int latestSegmentIndexForDate() const;
    // 날짜/기간이 바뀔 때 currentSegmentIndex를 latestSegmentIndexForDate()로 되돌리고 화면을 갱신한다.
    void resetToLatestSegment();
    // 구간 범위 라벨("14:20 ~ 14:30")을 currentDate/currentPeriodIndex/currentSegmentIndex 기준으로 갱신.
    void updateSegmentRangeLabel();

    QLabel *gasTitleLabel;
    QLabel *smokeTitleLabel;
    GasGraphWidget *gasGraph;
    GasGraphWidget *smokeGraph;

    QVector<QPushButton *> periodButtons; // 0=10분 1=1시간 2=6시간 3=하루
    QPushButton *dateButton;
    QPushButton *prevButton;
    QPushButton *todayButton;
    QPushButton *nextButton;
    QLabel *segmentRangeLabel; // 지금 보고 있는 구간의 실제 시각 범위 표시
    QLabel *legendLabel;
    QLabel *noteLabel;
    int currentPeriodIndex = 0;
    QDate currentDate;
    // currentDate 안에서 몇 번째 구간을 보고 있는지(0-based, periodMinutes(currentPeriodIndex) 단위로
    // 자정부터 정렬된 구간). 노션 확정안 — 기간 버튼은 "지금 기준 최근 N"이 아니라 "그 날짜 안 구간
    // 크기", ◀▶는 같은 날짜 안에서 구간 이동, 구간은 항상 :00/:10/:20... 정각 경계로 정렬된다.
    int currentSegmentIndex = 0;
    // requestCurrentPeriod()가 마지막으로 요청한 구간 경계(초) — 응답이 비동기로 오므로
    // loadSensorLogFromServer()에서 그래프 x축 정렬(시간 비율 배치)에 그대로 재사용한다.
    qint64 currentSegmentRangeFrom = 0;
    qint64 currentSegmentRangeTo = 0;
    // true면 매 분마다 최신 구간을 계속 따라간다(자동 전환 대상). ◀로 과거 구간으로 직접 이동하면
    // false가 되고, 다시 최신 구간까지 ▶로 돌아오거나 "현재"/기간·날짜 변경으로 리셋되면 true로.
    bool followLatestSegment = true;
    QString currentZoneId; // "A"~"D". updateZone()에서 바뀔 때만 재조회한다(매초 오는 sensor 메시지마다 X).

    // 사건 마커용. 그래프 x축은 표본 인덱스 기준이라 "시각 -> x비율" 변환에 실제 표본 시각이 필요하다.
    QVector<qint64> sampleTimes;
    struct EventStamp { qint64 ts = 0; bool danger = false; QString label; };
    QVector<EventStamp> eventStamps;
    // 그래프를 보는 동안 시간이 흘러도 화면이 그 자리에 멈춰있지 않도록 주기적으로 다시 조회한다.
    QTimer *liveRefreshTimer = nullptr;
};

#endif // GRAPHPAGE_H
