#ifndef SERVERLINK_H
#define SERVERLINK_H

#include <QObject>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include <QSslError>
#include "../core/DetectionTypes.h"
#include "../core/FloorMapTypes.h"
#include "../core/ArucoTypes.h"

class QSslSocket;
class QTimer;
class QJsonObject;
class QJsonArray;

// 라즈베리파이 서버와의 JSON 소켓 통신 전담.
// 수신: detection(계약①), sensor(계약②), led_matrix_status, actuator_status, control_ack, query_result
// 송신: control(수동 제어 명령), false_alarm_report, query(DB 조회)
class ServerLink : public QObject
{
    Q_OBJECT

public:
    explicit ServerLink(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);

    // 반환값: cmdId (응답 매칭용, controlResult/controlTimedOut 시그널에서 다시 옴)
    QString sendControl(const QString &zone, const QString &target, const QString &action, const QString &admin);
    // 비상 모드 전환/재실행/해제. zone은 "발생 구역 표시용"일 뿐 실제 적용은 전 구역. 
    // 응답은 emergencyResult/emergencyTimedOut.
    // cause: 정상 상태에서 전환할 때만 관리자가 지정(judgement.h Cause 값). 
    // 경고/위험이면 서버가 무시하고 자체 판단값을 씀.
    QString sendEmergencyTrigger(const QString &zone, const QString &cause, const QString &admin);
    // checklist: "현장 확인" 항목 키 목록(예: "gas_smell","valve_closed","personnel_returned"). 원인별로 다름.
    QString sendEmergencyClear(const QString &zone, const QString &admin, const QStringList &checklist);
    void sendFalseAlarmReport(int channel, int frameId, const QString &admin);
    // 관리자가 경고 팝업의 "확인"을 눌렀을 때만 호출. 무응답 자동 전환은 서버가 자체 타이머로 판단한다.
    void sendWarningAck(const QString &zone, const QString &admin);

    // DB 조회 요청. target: "event_log"/"sensor_log". extraParams는 from/to/zone/limit 등 쿼리별 파라미터.
    // 반환값 reqId로 queryResult/queryFailed 응답을 매칭한다.
    // ★ 서버가 아직 query 타입을 처리하지 않아서(2026-08 기준 db 쓰기 전용) 지금은 응답이 안 올 수 있음 — 뼈대.
    QString sendQuery(const QString &target, const QJsonObject &extraParams);

    // 감시 제외(ROI) 영역을 서버에 반영. channel은 1-based. overlapThreshold는 서버가 전역 0.5로
    // 고정 운용하기로 해서(PR #49 회신) 기본값만 그대로 실어 보낸다. 반환값 cmdId로 ignoreRegionsAck 매칭.
    QString sendSetIgnoreRegions(int channel, const QVector<RoiRegion> &regions, double overlapThreshold = 0.5);

    // 평면도 원본 이미지(PNG 바이트, 최대 6MB) 업로드 + 변환 요청. 반환값 cmdId로
    // floorMapUploadResult/floorMapUploadTimedOut 매칭. 이미지 저장 + EvacPlanner 변환까지
    // 걸리는 작업이라 control(3초)보다 타임아웃을 길게 둔다.
    QString sendSetFloorMap(const QByteArray &pngBytes);

    // ArUco 좌표 보정 설정 저장. 반환값 cmdId로 arucoConfigAck 매칭.
    QString sendSetArucoConfig(int channel, const ArucoChannelConfig &config);
    // 해당 채널 Homography 보정 실행 요청(서버가 서브프로세스로 계산, 수십 초 걸릴 수 있음).
    // 반환값 cmdId로 arucoCalibrationResult/arucoCalibrationTimedOut 매칭.
    QString sendStartArucoCalibration(int channel);

signals:
    void connectionStateChanged(bool connected);

    void detectionReceived(int channel, int frameId, int srcW, int srcH, bool alarm, const QVector<DetectionBox> &boxes);
    // cause: server/judgement.h Cause 네임스페이스 값(gas/smoke_visual/fire_confirmed 등). safe면 빈 문자열.
    // warnRemain: warning 상태일 때만 서버가 채워 보냄(무응답 자동 전환까지 남은 초). 그 외엔 -1.
    // flameVal: 불꽃센서(DFR0076) 전압(V). 클수록 강함.
    // responseOk: 목표 대응이 실제 액추에이터에 반영됐는가. 비상 모드 버튼 활성/비활성 판단에 씀.
    // clearSensor/clearVision/clearActuator: 해제 체크리스트 "시스템 확인" 3항목(서버 판정, emergency-mode #10).
    // sensorOk: 가스/연기/불꽃(ADS1115) 값을 믿을 수 있는가(10초 이상 못 읽으면 false).
    // dhtOk: 이번 틱에 온습도(DHT22)를 실제로 읽었는가(false=직전 값 재사용, emergency-mode #13).
    // dangerSource/admin: 자동 감지("auto")인지 수동 발령("manual")인지, 수동이면 발령자 이름 (emergency-mode #17).
    void sensorReceived(const QString &zone, qint64 ts, double temp, double humidity,
                         double gasPpm, double smokePpm, double flameVal, const QString &state,
                         const QString &cause, int warnRemain, bool responseOk,
                         bool clearSensor, bool clearVision, bool clearActuator,
                         bool sensorOk, bool dhtOk, const QString &dangerSource, const QString &admin);
    // 채널별(1~4) 서버 영상 감지 생존 여부. zone과 무관한 전역값이라 sensorReceived와 분리해서 보낸다.
    // (emergency-mode #14 — Qt 자체 영상 수신 여부와 조합해서 카메라 채널 점 4색 표시에 씀)
    void visionStatusReceived(bool ch1, bool ch2, bool ch3, bool ch4);
    void ledMatrixStatusReceived(int status);
    // link: "ok"/"down" (STM보드(1) 연결 상태, fan/valve/siren 공통 — 한 보드에서 오는 값이라 개별 구분 불가).
    // fanSrc/valveSrc/sirenSrc: 각각 "auto"/"manual" (액추에이터별 자동/수동, 위험 시 셋 다 자동으로
    // 바뀐 뒤 관리자가 하나만 수동 조작하는 경우가 있어 개별로 옴). 서버가 아직 안 보내는 구버전이면 빈 문자열.
    // targetFan/targetValve/targetSiren: 서버가 내리려 한 목표값. fan/valve/siren(STM이 실제 수용한 값)과
    // 비교해서 어느 장치가 명령 미반영인지 판별한다 (emergency-mode #15).
    // linkReason: STM 링크 끊김 사유 문구. 정상이거나 구버전 서버면 빈 문자열 (emergency-mode #16).
    void actuatorStatusReceived(int fan, int valve, int siren, const QString &link,
                                 const QString &fanSrc, const QString &valveSrc, const QString &sirenSrc,
                                 int targetFan, int targetValve, int targetSiren, const QString &linkReason);

    void controlResult(const QString &cmdId, const QString &zone, const QString &target,
                        const QString &result, const QString &reason);
    void controlTimedOut(const QString &cmdId, const QString &zone, const QString &target);

    // mode: "trigger"/"clear". emergency_ack 하나로 전환·재실행·해제 다 응답이 옴. 
    // 거절 없음 — result는 항상 "accepted".
    void emergencyResult(const QString &cmdId, const QString &zone, const QString &mode, const QString &result);
    void emergencyTimedOut(const QString &cmdId, const QString &zone, const QString &mode);

    // rows 구조는 target별로 다름 (명세서 3-1/3-2 참고). reqId로 어느 sendQuery() 응답인지 매칭.
    void queryResult(const QString &reqId, const QString &target, const QJsonArray &rows);
    void queryFailed(const QString &reqId, const QString &reason);

    // set_ignore_regions 응답. reason은 실패했을 때만 채워진다.
    void ignoreRegionsAck(const QString &cmdId, int channel, bool ok, const QString &reason);
    // 접속 직후 서버가 4채널 전부 push하거나, query target=ignore_regions 응답으로 온 것 —
    // Qt 입장에서 둘을 구분할 필요가 없어(그냥 최신값으로 화면에 반영하면 됨) 시그널 하나로 합쳤다.
    void ignoreRegionsReceived(int channel, double overlapThreshold, const QVector<RoiRegion> &regions);

    // set_floor_map 응답(floor_map_result). ok=false면 gridSize 이하 필드는 비어있다(reason 참고).
    void floorMapUploadResult(const QString &cmdId, bool ok, const QString &reason,
                               int gridSize, const QVector<QVector<int>> &bitmap,
                               const QVector<FloorMapMarker> &displays, const QVector<FloorMapMarker> &exits,
                               const QVector<FloorMapRoute> &routes);
    void floorMapUploadTimedOut(const QString &cmdId);
    // query target=floor_map 응답. available=false면 서버에 저장된 변환 결과가 아직 없음(result:"empty").
    void floorMapReceived(bool available, int gridSize, const QVector<QVector<int>> &bitmap,
                           const QVector<FloorMapMarker> &displays, const QVector<FloorMapMarker> &exits,
                           const QVector<FloorMapRoute> &routes);

    // set_aruco_config 응답. reason은 실패했을 때만 채워진다.
    void arucoConfigAck(const QString &cmdId, int channel, bool ok, const QString &reason);
    // query target=aruco_config 응답. config.configured가 false면 저장된 설정 없음.
    void arucoConfigReceived(int channel, const ArucoChannelConfig &config);
    // query target=aruco_status 응답 — 4채널 전부 한 번에.
    void arucoStatusReceived(const QVector<ArucoChannelStatus> &channels);
    // start_aruco_calibration 응답(aruco_calibration_result). ok=false면 accepted/detected/rms는
    // 의미 없음. detected는 v1 기준 "개수만"(어떤 ID인지는 아직 서버가 안 줌).
    void arucoCalibrationResult(const QString &cmdId, int channel, bool ok, const QString &reason,
                                 int acceptedMarkers, int detectedMarkers, double rmsPx);
    void arucoCalibrationTimedOut(const QString &cmdId, int channel);

    // query target=clip 응답. result: "ok"(data에 mp4 바이트) / "empty"(아직 저장 중,
    // 이벤트 후 약 15초 이내) / "error". ok가 아니면 data는 비어있다.
    void clipReceived(const QString &reqId, const QString &result, const QByteArray &data);

private slots:
    void onReadyRead();
    // 자체서명 인증서라 항상 발생하는 "신뢰할 수 없는 발급자" 에러를, 지문이 맞는 경우에만 무시한다.
    void onSslErrors(const QList<QSslError> &errors);

private:
    void handleLine(const QByteArray &line);
    void sendLine(const QJsonObject &obj);
    QString generateCmdId();
    QString sendEmergencyRequest(const QString &type, const QString &mode, const QString &zone,
                                  const QString &admin, const QJsonObject &extraFields);

    QSslSocket *socket;
    QByteArray buffer;
    QMap<QString, QTimer *> pendingCommands;
    // control과 별개 맵을 써서 evacuation_ack/타임아웃을 다른 시그널로 명확히 구분해 내보낸다.
    QMap<QString, QTimer *> pendingEmergencyCommands;
    // 평면도 업로드도 별개 맵 — UI가 한 번에 하나만 올리게 막지만, cmdId 매칭 규칙은 동일하게 맞춘다.
    QMap<QString, QTimer *> pendingFloorMapUploads;
    // ArUco 보정도 서버가 수십 초 걸릴 수 있는 서브프로세스라 별도 타임아웃 관리.
    QMap<QString, QTimer *> pendingArucoCalibrations;
    int cmdCounter = 0;
};

#endif // SERVERLINK_H
