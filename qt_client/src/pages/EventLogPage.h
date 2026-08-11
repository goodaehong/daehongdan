#ifndef EVENTLOGPAGE_H
#define EVENTLOGPAGE_H

#include <QWidget>
#include <QVector>
#include <QDateTime>
#include "../core/ZoneTypes.h"

class QTableWidget;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QTimeEdit;
class GasGraphWidget;
class QJsonArray;

struct EventEntry {
    QString date;        // 표시용 "yyyy-MM-dd"
    QString time;       // 표시용 "HH:mm:ss"
    QDateTime timestamp; // 기간 필터링용 실제 시각
    QString zone;
    QString detection;
    QString response;
    QString admin;
    QString severity;   // "안전"/"정보"/"경고"/"위험"
    QString sensorCombo;
    QString status;      // "해결됨"/"오탐 처리됨"
    QString duration;
};

// 이벤트 로그 화면 (세로 3분할): 좌상단 목록+필터, 좌하단 가스농도 그래프, 우측 상세 패널.
class EventLogPage : public QWidget
{
    Q_OBJECT

public:
    explicit EventLogPage(QWidget *parent = nullptr);

    void updateZone(const Zone &zone);
    void addEntry(const QString &zone, const QString &detection, const QString &response,
                  const QString &admin = "시스템(자동)", const QString &severity = "정보",
                  const QString &sensorCombo = "-", const QString &duration = "-");

    // 서버 query_result(target="event_log") 응답으로 목록을 통째로 교체한다.
    // rows 필드: id,ts,zone,category,severity,cause,sensorCombo,source,response,admin,
    //            gasPpm,smokePpm,status,durationMs,snapshotPath,incidentId (server/db/query_handler.cpp 참고)
    void loadEntriesFromServer(const QJsonArray &rows);

private slots:
    void applyFilter();
    void showDetail(int row, int column);
    void markFalseAlarm();
    void showDatePicker();
    void clearDateFilter();

private:
    void appendRow(const EventEntry &entry);

    QTableWidget *eventTable;
    QComboBox *zoneFilterCombo;
    QComboBox *severityFilterCombo;
    QComboBox *periodFilterCombo;
    QComboBox *statusFilterCombo;
    QLineEdit *searchEdit;
    // 그래프 화면과 동일한 방식(팝업 달력)의 특정 날짜 조회 + 시:분 직접 입력 시간대 조회.
    // periodFilterCombo(전체 기간/최근 1시간 등)와 별개로 AND 조건으로 함께 적용된다.
    QPushButton *dateButton;
    QDate filterDate; // 무효(QDate()) = 날짜 제한 없음
    QTimeEdit *startTimeEdit;
    QTimeEdit *endTimeEdit;
    QVector<EventEntry> eventEntries;
    GasGraphWidget *gasGraph;

    QLabel *detailPlaceholder;
    QWidget *detailContent;
    QLabel *detailTimeValue;
    QLabel *detailZoneValue;
    QLabel *detailAdminValue;
    QLabel *detailTypeValue;
    QLabel *detailSeverityValue;
    QLabel *detailSensorValue;
    QLabel *detailResponseValue;
    QLabel *detailStatusValue;
    QLabel *detailDurationValue;
    QPushButton *falseAlarmButton;
    int selectedEventRow = -1;
};

#endif // EVENTLOGPAGE_H
