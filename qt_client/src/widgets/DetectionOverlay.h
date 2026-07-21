#ifndef DETECTIONOVERLAY_H
#define DETECTIONOVERLAY_H

#include <QWidget>
#include <QVector>
#include "../core/DetectionTypes.h"

// video 위젯(native HWND) 위에 감지 박스를 그리는 별도의 always-on-top 투명 창.
// native 자식 위젯과 일반 Qt 자식 위젯을 같은 부모에 섞으면 Windows에서 컴포지팅이 깨지므로,
// 독립된 최상위 윈도우로 만들고 목표 위젯의 화면 좌표를 주기적으로 추적한다.
class DetectionOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit DetectionOverlay(QWidget *followTarget);

    void setBoxes(const QVector<DetectionBox> &boxes, int srcW, int srcH);
    void syncGeometry();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QWidget *target;
    QVector<DetectionBox> boxes;
    int srcWidth = 0;
    int srcHeight = 0;
};

#endif // DETECTIONOVERLAY_H
