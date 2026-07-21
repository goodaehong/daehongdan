#ifndef CONTROLPAGE_H
#define CONTROLPAGE_H

#include <QWidget>
#include <QVector>
#include <functional>

class QLabel;
class QPushButton;

// 수동 제어 화면: 환기팬(4단계)/밸브/사이렌 카드. 클릭 시 확인 다이얼로그를 거쳐 실행.
class ControlPage : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPage(QWidget *parent = nullptr);
    void setZoneName(const QString &zoneName);

    // 서버 actuator_status의 fan 값을 반영해 현재 선택된 단계를 하이라이트한다.
    // level: 0=OFF, 1=약, 2=중, 3=강. 그 외 값(-1 등)이면 전부 비활성 표시.
    void setFanLevel(int level);

    // valve: 0=잠금, 1=개방 / siren: 0=OFF, 1=ON. 그 외 값(-1 등)이면 "확인 중"으로 표시.
    void setValveState(int state);
    void setSirenState(int state);

signals:
    // 확인 다이얼로그 통과 직후 발생. 실제 성공/실패는 서버의 control_ack로 판단하므로
    // 여기서는 로그를 남기지 않고 요청만 올린다 (MainWindow가 ServerLink로 전송).
    void controlRequested(const QString &target, const QString &action, const QString &title);

private:
    // actionTitleProvider: 클릭 시점의 토글 방향에 맞는 확인창 문구("밸브 개방"/"밸브 잠금" 등)를 돌려준다.
    QWidget *createControlCard(const QString &title, const QString &desc,
                                const std::function<QString()> &actionTitleProvider,
                                const std::function<void()> &onConfirm,
                                QLabel **statusLabelOut = nullptr);
    QWidget *createFanControlCard();
    void updateFanButtonStyles(int activeLevel);
    bool showConfirmDialog(const QString &actionName);

    QLabel *titleLabel;
    QVector<QPushButton *> fanButtons; // [0]=OFF [1]=약 [2]=중 [3]=강
    int currentFanLevel = -1; // 클릭 시 낙관적으로 갱신, actuator_status 오면 덮어씀

    QLabel *valveStatusLabel = nullptr;
    QLabel *sirenStatusLabel = nullptr;
    int currentValveState = -1; // 0=잠금, 1=개방
    int currentSirenState = -1; // 0=OFF, 1=ON
};

#endif // CONTROLPAGE_H
