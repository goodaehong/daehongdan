#ifndef DETECTIONOVERLAY_H
#define DETECTIONOVERLAY_H

#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include "../core/DetectionTypes.h"

// video 위젯(native HWND) 위에 감지 박스를 그리는 별도의 always-on-top 투명 창.
// native 자식 위젯과 일반 Qt 자식 위젯을 같은 부모에 섞으면 Windows에서 컴포지팅이 깨지므로,
// 독립된 최상위 윈도우로 만들고 목표 위젯의 화면 좌표를 주기적으로 추적한다.
class DetectionOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit DetectionOverlay(QWidget *followTarget);
    ~DetectionOverlay() override;

    void setBoxes(const QVector<DetectionBox> &boxes, int srcW, int srcH);
    void setViewTransform(double factor, double panX, double panY);
    void syncGeometry();

    // 감시 제외 ROI 편집 모드(오늘 범위: Qt 로컬 상태만, 서버 전송은 이후 작업).
    // 편집 중엔 줌/이동과 무관하게 항상 입력을 받고, 좌표는 zoom=1.0/pan=0 기준 화면 비율(0~1)로
    // 저장한다 — 실제 소스 해상도(srcW/srcH)는 감지 이벤트가 온 적 있어야만 알 수 있어서
    // 지금은 그 값에 의존하지 않는다. 서버 연동 시 원본 프레임 좌표계로 다시 맞춰야 한다.
    void setRoiEditMode(bool enabled);
    void setRoiRegions(const QVector<QRectF> &normalizedRects);
    QVector<QRectF> roiRegions() const { return roiRects; }

signals:
    void dragDelta(const QPoint &delta);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // ROI 사각형의 화면 좌표(screenRect)를 받아 우상단 삭제(×) 배지의 화면 좌표를 계산한다.
    // paintEvent(그리기)와 eventFilter(클릭 판정) 양쪽에서 똑같은 위치를 써야 해서 분리했다.
    QRectF roiDeleteBadgeRect(const QRectF &screenRect) const;

    QWidget *target;
    QWidget *inputSurface;
    QVector<DetectionBox> boxes;
    int srcWidth = 0;
    int srcHeight = 0;
    double zoomFactor = 1.0;
    double panX = 0.0;
    double panY = 0.0;
    bool interactionEnabled = false;
    bool dragging = false;
    QPoint lastDragPosition;

    bool roiEditMode = false;
    QVector<QRectF> roiRects;   // 0~1 정규화, zoom=1.0/pan=0 기준 화면 좌표계
    bool roiDrawing = false;
    QPointF roiDragStart;
    QRectF roiDragCurrentScreen;
};

#endif // DETECTIONOVERLAY_H
