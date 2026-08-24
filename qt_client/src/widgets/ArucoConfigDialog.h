#ifndef ARUCOCONFIGDIALOG_H
#define ARUCOCONFIGDIALOG_H

#include <QDialog>
#include "../core/ArucoTypes.h"

class QTableWidget;
class QPushButton;
class QLabel;
class QDoubleSpinBox;
class ServerLink;

// 채널 하나의 ArUco 좌표(공장/모형/보드 범위 + 마커 배치)를 입력·저장하는 폼.
// ArucoCalibrationDialog의 "좌표 설정" 버튼에서 채널별로 하나씩 띄운다.
// 열리면 기존 설정을 조회해서 채워 넣고(query target=aruco_config), 저장은 set_aruco_config로 보낸다.
class ArucoConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ArucoConfigDialog(ServerLink *serverLink, int channel, QWidget *parent = nullptr);

private slots:
    void onArucoConfigReceived(int channel, bool available, const ArucoChannelConfig &config);
    void onArucoConfigResult(int channel, bool ok, const QString &reason);
    void onAddMarkerRow();
    void onSaveClicked();

private:
    void fillForm(const ArucoChannelConfig &config);
    // 마커 테이블 한 줄을 추가하고 그 안의 스핀박스들을 반환(값 채우기용). id가 -1이면 자동 배정.
    void addMarkerRow(int id, double x, double y, double sizeCm, double rotation);
    // 폼 내용을 ArucoChannelConfig로. 검증 실패 시 false + errorOut에 사유(한글).
    bool collectConfig(ArucoChannelConfig &out, QString *errorOut) const;

    ServerLink *serverLink;
    int channel;
    QString pendingQueryReqId;

    QDoubleSpinBox *factoryMinX, *factoryMinY, *factoryMaxX, *factoryMaxY;
    QDoubleSpinBox *modelScale;
    QDoubleSpinBox *boardMinX, *boardMinY, *boardMaxX, *boardMaxY;
    QTableWidget *markerTable = nullptr;
    QLabel *statusLabel = nullptr;
    QPushButton *saveButton = nullptr;
};

#endif // ARUCOCONFIGDIALOG_H
