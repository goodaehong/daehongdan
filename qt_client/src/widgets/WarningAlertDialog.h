#ifndef WARNINGALERTDIALOG_H
#define WARNINGALERTDIALOG_H

#include <QDialog>

class QLabel;

// 경고(sensor state=="warning") 발생 시 뜨는 관리자 알림 팝업.
// "확인" 클릭 -> acknowledged만 emit. 무응답 자동 전환 판단은 전부 서버 책임이라
// Qt는 자체 타이머를 돌리지 않고, sensor 메시지의 warnRemain 값을 받은 그대로 표시만 한다.
class WarningAlertDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WarningAlertDialog(const QString &zoneName, const QString &cause,
                                 int initialRemainSeconds, QWidget *parent = nullptr);

    // 서버 sensor 메시지의 warnRemain을 그대로 반영. 0 이하면 카운트다운 문구를 감춘다.
    void setRemainingSeconds(int seconds);

signals:
    void acknowledged(); // 관리자가 확인 버튼을 눌렀을 때만 emit (무응답으로는 절대 emit 안 함)

private:
    QLabel *countdownNumberLabel;
    QLabel *countdownLabel;
};

#endif // WARNINGALERTDIALOG_H
