#ifndef GRAPHPAGE_H
#define GRAPHPAGE_H

#include <QWidget>
#include "../core/ZoneTypes.h"

class QLabel;
class GasGraphWidget;

// 그래프 화면: 가스농도(CO)와 연기 위험도를 나란히 표시.
class GraphPage : public QWidget
{
    Q_OBJECT

public:
    explicit GraphPage(QWidget *parent = nullptr);
    void updateZone(const Zone &zone);

private:
    QLabel *gasTitleLabel;
    QLabel *smokeTitleLabel;
    GasGraphWidget *gasGraph;
    GasGraphWidget *smokeGraph;
};

#endif // GRAPHPAGE_H
