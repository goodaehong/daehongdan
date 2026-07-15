#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVector>
#include <functional>

class QStackedWidget;
class QTableWidget;
class QComboBox;
class QLineEdit;
class QLabel;
class QVideoWidget;
class QPushButton;
class GasGraphWidget;

enum class ZoneState { Safe, Warning, Danger };

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

signals:
    void loggedOut();

private slots:
    void applyEventFilter();
    void showEventDetail(int row, int column);
    void markFalseAlarm();

private:
    struct Zone {
        QString name;
        ZoneState state = ZoneState::Safe;
        double temp;
        double humidity;
    };

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

    QWidget *createTopBar();
    QWidget *createSubTabBar();
    QWidget *createMonitoringPage();
    QWidget *createEventLogPage();
    QWidget *createGraphPage();
    QWidget *createManualControlPage();
    QWidget *createHelpPage();

    QLabel *createGlowCircle(int diameter);
    QWidget *createCameraTile(int channel);
    QWidget *createControlCard(const QString &title, const QString &desc, const std::function<void()> &onConfirm);
    bool showConfirmDialog(const QString &actionName);

    void switchTab(int index);
    void switchZone(int index);
    void setZoneState(int zoneIndex, ZoneState state);
    void refreshZoneUi();
    void addEventLog(const QString &zone, const QString &detection, const QString &response,
                      const QString &admin = "시스템(자동)", const QString &severity = "정보",
                      const QString &sensorCombo = "-", const QString &duration = "-");

    QStackedWidget *stack;
    QList<QPushButton *> tabButtons;
    QList<QPushButton *> zoneButtons;
    QList<Zone> zones;
    int currentZone = 0;

    // monitoring page
    QLabel *heroTitleLabel;
    QLabel *heroCircle;
    QLabel *heroStateLabel;
    QLabel *tempValueLabel;
    QLabel *humidityValueLabel;
    QLabel *gasValueLabel;
    QLabel *smokeValueLabel;
    QLabel *cameraStatusValueLabel;
    QList<QPushButton *> demoStateButtons;
    QList<QLabel *> cameraTitleLabels;

    QVideoWidget *zoneVideoWidget;
    QMediaPlayer *player;
    QAudioOutput *audioOutput;

    // event log page
    QTableWidget *eventTable;
    QComboBox *zoneFilterCombo;
    QLineEdit *searchEdit;
    QVector<EventEntry> eventEntries;
    GasGraphWidget *eventLogGasGraph;

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

    // graph page
    GasGraphWidget *gasGraph;
    GasGraphWidget *smokeGraph;
    QLabel *graphTitleLabel;
    QLabel *smokeGraphTitleLabel;

    // manual control page
    QLabel *manualTitleLabel;
};
#endif // MAINWINDOW_H
