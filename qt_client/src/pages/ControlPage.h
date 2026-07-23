#ifndef CONTROLPAGE_H
#define CONTROLPAGE_H

#include <QWidget>
#include <QVector>

class QLabel;
class QPushButton;

// 수동 제어 화면: 환기팬(4단계)/밸브(2단계)/사이렌(2단계) 카드.
// 셋 다 버튼 + 확인 다이얼로그 + 현재 상태 하이라이트로 동일한 방식.
class ControlPage : public QWidget
{
    Q_OBJECT

public:
    explicit ControlPage(QWidget *parent = nullptr);
    void setZoneName(const QString &zoneName);

    // 서버 actuator_status 값을 반영해 현재 선택된 버튼을 하이라이트한다. -1(등)이면 전부 비활성 표시.
    void setFanLevel(int level);   // 0=OFF, 1=약, 2=중, 3=강
    void setValveState(int state); // 0=잠금, 1=개방
    void setSirenState(int state); // 0=OFF, 1=ON

signals:
    // 확인 다이얼로그 통과 직후 발생. 실제 성공/실패는 서버의 control_ack로 판단하므로
    // 여기서는 로그를 남기지 않고 요청만 올린다 (MainWindow가 ServerLink로 전송).
    void controlRequested(const QString &target, const QString &action, const QString &title);

private:
    QWidget *createFanControlCard();
    QWidget *createValveControlCard();
    QWidget *createSirenControlCard();
    QWidget *createEvacuationCard();
    void updateButtonStyles(QVector<QPushButton *> &buttons, int activeIndex);
    void updateStatusLabel(QLabel *label, QVector<QPushButton *> &buttons, int activeIndex);
    bool showConfirmDialog(const QString &actionName);

    QLabel *titleLabel;

    QVector<QPushButton *> fanButtons;   // [0]=OFF [1]=약 [2]=중 [3]=강
    QVector<QPushButton *> valveButtons; // [0]=잠금 [1]=개방
    QVector<QPushButton *> sirenButtons; // [0]=OFF [1]=ON

    QLabel *fanStatusLabel = nullptr;
    QLabel *valveStatusLabel = nullptr;
    QLabel *sirenStatusLabel = nullptr;

    int currentFanLevel = -1;
    int currentValveState = -1;
    int currentSirenState = -1;
};

#endif // CONTROLPAGE_H
