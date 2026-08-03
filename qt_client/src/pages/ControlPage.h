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
    void actionLogged(const QString &detection, const QString &response,
                       const QString &admin, const QString &severity,
                       const QString &sensorCombo, const QString &duration);

private:
    QWidget *createControlCard(const QString &title, const QString &desc, const std::function<void()> &onConfirm);
    bool showConfirmDialog(const QString &actionName);

    QLabel *titleLabel;
};

#endif // CONTROLPAGE_H
