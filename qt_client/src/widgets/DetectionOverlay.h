#ifndef DETECTIONOVERLAY_H
#define DETECTIONOVERLAY_H

#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QPolygonF>
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

    // 감시 제외 ROI 편집 모드. 편집 중엔 줌/이동과 무관하게 항상 입력을 받는다.
    // 저장 좌표는 화면 비율이 아니라 **원본 프레임 기준 0~1**이다(PR #50 리뷰 — 대홍) — 감지 박스와
    // 똑같이 크롭·줌·팬 보정을 거쳐야 서버가 실제로 잘라내는 영역과 화면에 그려지는 영역이 일치한다.
    // frameToScreen()/screenToFrameFraction()이 그 변환을 담당한다.
    //
    // 영역은 직사각형이 아니라 꼭짓점 4개를 직접 찍는 사각형(기울어진 설비/벽면도 감쌀 수 있게)이며,
    // 프로토콜의 points[[x,y],…] 형식과 그대로 대응된다. 영역마다 적용 대상(화재/연기)도 지정한다.
    void setRoiEditMode(bool enabled);
    void setRoiRegions(const QVector<RoiRegion> &regions);
    QVector<RoiRegion> roiRegions() const { return roiRegionList; }
    // 편집이 끝난 뒤에도 영역이 계속 영상 위에 겹쳐 보이면 감시에 방해가 된다 —
    // 평상시 표시 여부를 끌 수 있게 한다(편집 모드에서는 이 값과 무관하게 항상 보인다).
    void setRoiVisible(bool visible);
    bool isRoiVisible() const { return roiVisible; }

signals:
    void dragDelta(const QPoint &delta);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // ROI 영역의 화면 좌표를 받아 우상단 삭제(×) 배지의 화면 좌표를 계산한다.
    // paintEvent(그리기)와 eventFilter(클릭 판정) 양쪽에서 똑같은 위치를 써야 해서 분리했다.
    QRectF roiDeleteBadgeRect(const QPolygonF &screenPoly) const;
    // 적용 대상(화재/연기) 전환 배지. 클릭할 때마다 둘다 -> 화재만 -> 연기만 순으로 돈다.
    QRectF roiApplyBadgeRect(const QPolygonF &screenPoly) const;
    // 영역 이름 표시/입력 칸. 적용 대상 배지 오른쪽에 나란히 놓는다.
    QRectF roiLabelRect(const QPolygonF &screenPoly) const;
    // 이름 입력 다이얼로그. 확인을 누르면 true를 주고 label을 채운다.
    // QInputDialog를 안 쓰는 이유는 구현부 주석 참고(테마·always-on-top 문제).
    bool promptRoiLabel(QString &label);
    // 원본 프레임 기준 0~1 폴리곤 -> 현재 위젯 크기의 화면 좌표 폴리곤 (frameToScreen을 각 점에 적용).
    QPolygonF roiToScreen(const QPolygonF &normalized) const;
    // 화면 좌표에서 꼭짓점을 집는다. 못 찾으면 polyIndex=-1.
    void hitTestVertex(const QPointF &pos, int &polyIndex, int &vertexIndex) const;

    // 감지 박스 렌더링(paintEvent)이 쓰는 것과 동일한 크롭/줌/팬 보정값. 두 곳에서 각자 계산하면
    // 하나만 고치고 잊어버리는 사고가 나기 쉬워서(이번 버그가 그 경우) 여기 하나로 합쳤다.
    struct CropTransform { double cropLeft, cropTop, scaleX, scaleY; };
    CropTransform currentCropTransform() const;
    // 원본 프레임 기준 0~1 좌표 -> 현재 화면(위젯) 픽셀 좌표. 감지 박스와 ROI 표시가 공유한다.
    QPointF frameToScreen(const QPointF &frameFraction) const;
    // 화면(위젯) 픽셀 좌표 -> 원본 프레임 기준 0~1 좌표. ROI를 서버로 보내기 전 역변환에 쓴다.
    // srcWidth/srcHeight를 아직 모르면(감지 메시지를 한 번도 못 받았으면) 위젯 크기 비율로 대체한다.
    QPointF screenToFrameFraction(const QPointF &screenPixel) const;
    // 영역의 적용 대상에 따른 표시색/문구 — 감지 박스 색상 언어(FIRE=빨강, SMOKE=주황)와 맞춘다.
    static QColor roiColorFor(const RoiRegion &region);
    static QString roiApplyText(const RoiRegion &region);

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
    bool roiVisible = true;            // 평상시(편집 아닐 때) 영역을 영상 위에 표시할지
    // 모달 다이얼로그가 떠 있는 동안 true. 이 창들은 always-on-top이라 그대로 두면 다이얼로그를
    // 덮어버리는데, 주기 실행되는 syncGeometry()가 자동으로 다시 띄우기 때문에 플래그로 막아야 한다.
    bool modalDialogOpen = false;
    QVector<RoiRegion> roiRegionList;  // 각 영역 = 꼭짓점 4개(0~1 정규화) + 적용 대상 + 이름
    // 지금 찍고 있는 꼭짓점들(화면 좌표). 4개가 되면 roiPolys로 확정되고 비워진다.
    QVector<QPointF> pendingVertices;
    QPointF hoverPos;              // 다음 변이 어디로 이어질지 미리 보여주기 위한 현재 커서 위치
    bool hasHoverPos = false;
    // 이미 만든 영역의 꼭짓점을 끌어서 미세 조정하는 중인지. -1이면 조정 중 아님.
    int dragPolyIndex = -1;
    int dragVertexIndex = -1;
};

#endif // DETECTIONOVERLAY_H
