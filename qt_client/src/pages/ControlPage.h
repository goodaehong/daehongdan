#ifndef CONTROLPAGE_H
#define CONTROLPAGE_H

#include <QWidget>
#include <functional>

class QLabel;

// 수동 제어 화면: 환기팬/밸브/사이렌 카드. 클릭 시 확인 다이얼로그를 거쳐 실행.
class ControlPage : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPage(QWidget *parent = nullptr);
    void setZoneName(const QString &zoneName);

signals:
    // 확인 다이얼로그 통과 직후 발생. 실제 성공/실패는 서버의 control_ack로 판단하므로
    // 여기서는 로그를 남기지 않고 요청만 올린다 (MainWindow가 ServerLink로 전송).
    void controlRequested(const QString &target, const QString &action, const QString &title);

private:
    QWidget *createControlCard(const QString &title, const QString &desc, const std::function<void()> &onConfirm);
    bool showConfirmDialog(const QString &actionName);

    QLabel *titleLabel;
};

#endif // CONTROLPAGE_H
