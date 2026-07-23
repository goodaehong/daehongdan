#ifndef WARNINGALERTDIALOG_H
#define WARNINGALERTDIALOG_H

#include <QDialog>

class QLabel;
class QTimer;

// 경고(sensor state=="warning") 발생 시 뜨는 관리자 알림 팝업.
// "확인" 클릭 -> acknowledged, 카운트다운 만료까지 무응답 -> timedOut.
// countdownSeconds/원인문구는 서버가 warnRemain/cause 필드를 보내주기 전까지 고정값을 쓴다.
class WarningAlertDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WarningAlertDialog(const QString &zoneName, const QString &cause,
                                 int countdownSeconds, QWidget *parent = nullptr);

signals:
    void acknowledged();
    void timedOut();

private:
    QLabel *countdownLabel;
    QTimer *timer;
    int remaining;
};

#endif // WARNINGALERTDIALOG_H
