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
class QTimer;
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
    // 이벤트 시점 13초(전 3초+후 10초) mp4 클립 경로. 비어있으면 클립 없음(수동제어 등) 또는
    // 아직 저장 중(이벤트 후 약 15초 이내 — PR #63). 서버 원본 그대로 query target=clip에 실어보낸다.
    QString clipPath;
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

public slots:
    // query target=clip 응답. reqId가 지금 대기 중인 요청과 다르면 무시(다른 행 클릭 등으로
    // 이미 stale해진 응답).
    void onClipReceived(const QString &reqId, const QString &result, const QByteArray &data);
    // MainWindow가 sendQuery("clip", ...)로 받은 reqId를 여기 등록해서, 나중에 onClipReceived가
    // "지금 기다리는 그 요청" 응답인지 판별하게 한다.
    void trackClipRequest(const QString &reqId);
    // 조회 범위(날짜 필터가 있으면 그 하루, 없으면 최근 24시간) 계산해서 eventLogRequested emit.
    // MainWindow가 실제 이벤트(상태 전환/제어 성공/비상 전환·해제 등) 발생 시점마다 바로 호출해서
    // 이벤트로그가 사실상 실시간으로 갱신되게 한다 — 30초 주기는 그 사이 놓친 것만 잡는 안전망.
    void requestRefresh();

signals:
    // "조회" 클릭 또는 주기적 자동 갱신 때 발생 — MainWindow가 받아서 서버에 다시 query를 보낸다.
    // 예전엔 최초 접속 시 1회만 불러오고 끝이라, 그 이후 발생한 이벤트(비상 전환/해제 등)가
    // 화면에 영영 안 보였다. "조회" 버튼도 지금까지 이미 불러온 목록을 필터링만 할 뿐 서버에
    // 다시 물어보지 않았던 게 원인.
    void eventLogRequested(qint64 from, qint64 to);
    // "재생" 클릭 시 발생 — MainWindow가 받아서 serverLink->sendQuery("clip", {"path":path})로 넘긴다.
    void clipPlayRequested(const QString &path);

private slots:
    void applyFilter();
    void showDetail(int row, int column);
    void markFalseAlarm();
    void showDatePicker();
    void clearDateFilter();
    void onPlayClipClicked();

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
    QTimer *refreshTimer = nullptr;
    bool columnsAutoSized = false; // 컬럼 폭 자동 맞춤은 최초 1회만 — 이후엔 사용자가 드래그한 폭 유지

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

    // 이벤트 클립(PR #63) 재생 UI. clipStatusLabel은 "스냅샷 없음"/"저장 중"/에러 문구를,
    // clipPlayButton은 clipPath가 있을 때만 보여준다.
    QLabel *clipStatusLabel;
    QPushButton *clipPlayButton;
    QString pendingClipReqId;   // 지금 기다리는 clip 요청. 비어있으면 대기 중인 요청 없음
};

#endif // EVENTLOGPAGE_H
