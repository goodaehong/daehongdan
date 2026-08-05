#ifndef GRAPHPAGE_H
#define GRAPHPAGE_H

#include <QWidget>
#include <QVector>
#include <QDate>
#include "../core/ZoneTypes.h"

class QLabel;
class QPushButton;
class GasGraphWidget;

// 그래프 화면: 가스농도/연기농도 추이 + 기간·날짜 선택 UI.
// 기간/날짜 선택은 서버 DB 조회(query/query_result) 프로토콜이 아직 없어서 뼈대만 만들어둠 —
// 실제 연동되면 selectPeriod()/이전·다음 버튼 클릭 지점에서 ServerLink::sendQuery()만 호출하면 됨.
class GraphPage : public QWidget
{
    Q_OBJECT

public:
    explicit GraphPage(QWidget *parent = nullptr);
    void updateZone(const Zone &zone);

private:
    QWidget *createControlBar();
    void selectPeriod(int index);
    void updateNavButtons();

    QLabel *gasTitleLabel;
    QLabel *smokeTitleLabel;
    GasGraphWidget *gasGraph;
    GasGraphWidget *smokeGraph;

    QVector<QPushButton *> periodButtons; // 0=10분 1=1시간 2=6시간 3=하루
    QPushButton *dateButton;
    QPushButton *prevButton;
    QPushButton *todayButton;
    QPushButton *nextButton;
    QLabel *legendLabel;
    QLabel *noteLabel;
    int currentPeriodIndex = 0;
    QDate currentDate;
};

#endif // GRAPHPAGE_H
