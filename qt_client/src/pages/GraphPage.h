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
};

#endif // GRAPHPAGE_H
