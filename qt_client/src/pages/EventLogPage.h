#ifndef EVENTLOGPAGE_H
#define EVENTLOGPAGE_H

#include <QWidget>
#include <QVector>
#include "../core/ZoneTypes.h"

class QTableWidget;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;
class GasGraphWidget;

struct EventEntry {
    QString time;
    QString zone;
    QString detection;
    QString response;
    QString admin;
    QString severity;
    QString sensorCombo;
    QString status;
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

private slots:
    void applyFilter();
    void showDetail(int row, int column);
    void markFalseAlarm();

private:
    QTableWidget *eventTable;
    QComboBox *zoneFilterCombo;
    QLineEdit *searchEdit;
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
