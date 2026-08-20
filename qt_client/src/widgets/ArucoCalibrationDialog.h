#ifndef ARUCOCALIBRATIONDIALOG_H
#define ARUCOCALIBRATIONDIALOG_H

#include <QDialog>
#include "../core/ArucoTypes.h"

class QComboBox;
class QDoubleSpinBox;
class QTableWidget;
class QPushButton;
class QLabel;
class ServerLink;

// 관리자 전용 "카메라 좌표 보정" 화면. 재환님 확정안 그대로:
// Qt는 입력/상태표시만, 검증·파일저장·Homography 계산은 전부 서버가 한다.
// v1 범위: 마커 ID 자동검색 없음(직접 입력), 마커 방향은 항상 기본값(0, 서버가 자동판별),
// 진행률 표시 없음("보정 중..." 텍스트로 대체), 검출 마커는 ID 목록 아니라 개수만 표시.
//
// 참고(2026-08-20): 서버가 이 설정을 실시간 감지에 실제로 적용하는 배선(worker() 쪽
// 자동 로드/핫스왑)은 아직 없음(별도 작업 중). 저장·보정까지는 정상 동작하지만
// 그 결과가 화재감지에 반영되는 건 그 작업이 끝난 뒤부터다.
class ArucoCalibrationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ArucoCalibrationDialog(ServerLink *serverLink, QWidget *parent = nullptr);

private slots:
    void onChannelChanged(int index);
    void onLoadClicked();
    void onSaveClicked();
    void onCalibrateClicked();
    void onAddMarkerRow();
    void onRemoveSelectedMarkerRow();

    void onArucoConfigReceived(int channel, const ArucoChannelConfig &config);
    void onArucoConfigAck(const QString &cmdId, int channel, bool ok, const QString &reason);
    void onArucoStatusReceived(const QVector<ArucoChannelStatus> &channels);
    void onArucoCalibrationResult(const QString &cmdId, int channel, bool ok, const QString &reason,
                                   int acceptedMarkers, int detectedMarkers, double rmsPx);
    void onArucoCalibrationTimedOut(const QString &cmdId, int channel);

private:
    int currentChannel() const;
    void applyConfigToForm(const ArucoChannelConfig &config);
    ArucoChannelConfig collectConfigFromForm() const;
    void setBusy(bool busy, const QString &statusText);

    ServerLink *serverLink;

    QComboBox *channelCombo = nullptr;
    QPushButton *loadButton = nullptr;
    QPushButton *saveButton = nullptr;
    QPushButton *calibrateButton = nullptr;

    QDoubleSpinBox *factoryMinXSpin = nullptr, *factoryMinYSpin = nullptr;
    QDoubleSpinBox *factoryMaxXSpin = nullptr, *factoryMaxYSpin = nullptr;
    QDoubleSpinBox *modelScaleSpin = nullptr;
    QDoubleSpinBox *boardMinXSpin = nullptr, *boardMinYSpin = nullptr;
    QDoubleSpinBox *boardMaxXSpin = nullptr, *boardMaxYSpin = nullptr;
    QDoubleSpinBox *commonMarkerSizeSpin = nullptr;

    QTableWidget *markerTable = nullptr;
    QLabel *statusLabel = nullptr;
    QTableWidget *channelStatusTable = nullptr;

    QString pendingSaveCmdId;
    QString pendingCalibrationCmdId;
};

#endif // ARUCOCALIBRATIONDIALOG_H
