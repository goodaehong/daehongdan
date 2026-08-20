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

// 관리자 전용 "카메라 좌표 보정 상태" 화면 (PR #65 기준으로 재작성, 2026-08-20).
//
// 실제 서버 범위는 애초 확정안보다 좁다 — Qt는 상태 조회 + 재로드 요청만 하고,
// 좌표 설정 자체(aruco_board_config.txt 작성)는 여전히 SSH로 수동 작업한다.
// (예전엔 Qt에서 좌표를 입력해 저장하는 화면을 만들었었는데, 그 프로토콜을 서버가
// 구현하지 않아서 걷어내고 이 화면으로 교체함.)
class ArucoCalibrationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ArucoCalibrationDialog(ServerLink *serverLink, QWidget *parent = nullptr);

private slots:
    void onRefreshClicked();
    void onReloadClicked(int channel);

    void onCalibStatusReceived(const QVector<CalibChannelStatus> &channels);
    void onCalibReloadResult(int channel, bool accepted, const QString &reason);

private:
    void requestStatus();

    ServerLink *serverLink;

    QPushButton *refreshButton = nullptr;
    QLabel *statusLabel = nullptr;
    QTableWidget *table = nullptr;
    // 재로드는 완료 응답이 없어서(접수만 옴), 몇 초 뒤 자동으로 다시 조회해서 반영한다.
    QTimer *autoRefreshTimer = nullptr;
};

#endif // ARUCOCALIBRATIONDIALOG_H
