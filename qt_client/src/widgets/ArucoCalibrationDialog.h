#ifndef ARUCOCALIBRATIONDIALOG_H
#define ARUCOCALIBRATIONDIALOG_H

#include <QDialog>
#include <QVector>
#include "../core/ArucoTypes.h"

class QTableWidget;
class QPushButton;
class QLabel;
class QTimer;
class ServerLink;

// 관리자 전용 "카메라 좌표 보정" 화면 (2026-08-21, 좌표 입력·실행 프로토콜 반영).
//
// 채널별로 상태를 한눈에 보여주고, 행마다 세 가지 동작을 할 수 있다:
//   설정 — ArucoConfigDialog로 공장/모형/보드 범위와 마커 좌표를 입력·저장
//   실행/중단 — 서버가 실제로 마커를 검출해 보정 계산을 돌리는 것을 시작/중단
//   재로드 — 이미 계산된 보정 파일을 서버 재시작 없이 다시 읽어 적용
class ArucoCalibrationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ArucoCalibrationDialog(ServerLink *serverLink, QWidget *parent = nullptr);

private slots:
    void onRefreshClicked();
    void onReloadClicked(int channel);
    void onConfigClicked(int channel);
    void onRunClicked(int channel);
    void onCancelClicked(int channel);

    void onCalibStatusReceived(const QVector<CalibChannelStatus> &channels);
    void onCalibReloadResult(int channel, bool accepted, const QString &reason);
    void onRunCalibrationResult(int channel, bool accepted, const QString &reason);
    void onCancelCalibrationResult(int channel, bool accepted, const QString &reason);
    // 서버가 먼저 보내는 완료 알림 — ok/error/cancelled/timeout. cancelled는 오류로 안 보여준다.
    void onCalibrationDone(int channel, const QString &result);

private:
    void requestStatus();

    ServerLink *serverLink;

    QPushButton *refreshButton = nullptr;
    QLabel *statusLabel = nullptr;
    QTableWidget *table = nullptr;
    // 재로드/실행/중단은 완료 응답이 없어서(접수만 옴), 몇 초 뒤 자동으로 다시 조회해서 반영한다.
    // calibration_done을 받았을 때도 즉시 같은 타이머로 재조회한다.
    QTimer *autoRefreshTimer = nullptr;
};

#endif // ARUCOCALIBRATIONDIALOG_H
