#ifndef FLOORMAPGRIDWIDGET_H
#define FLOORMAPGRIDWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPoint>
#include "../core/FloorMapTypes.h"

class QTimer;

// 대피도 격자를 그린다: 장애물/통행가능 셀, 전광판(주황)/출구(초록) 마커, 선택된 대피경로.
// 서버가 이미지를 안 보내고 격자 데이터만 주는 이유는 평면도 대피경로 제안 문서 참고 —
// Qt가 이미 ROI/감지박스/그래프를 전부 직접 그리고 있어서 같은 패턴을 따른다.
class FloorMapGridWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FloorMapGridWidget(QWidget *parent = nullptr);

    // bitmap[y][x]: 0=통행가능, 1=장애물. gridSize x gridSize 정사각형이어야 한다. 크기는
    // 하드코딩하지 않고 매번 서버가 보내주는 gridSize를 그대로 씀(2026-08-19 기준 60,
    // server/evac_map_tools/EvacPlanner.h의 GRID_SIZE와 항상 일치).
    void setMapData(int gridSize, const QVector<QVector<int>> &bitmap,
                     const QVector<FloorMapMarker> &displays, const QVector<FloorMapMarker> &exits,
                     const QVector<FloorMapRoute> &routes);
    void clearMapData();

signals:
    void displayClicked(int displayId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    // 위젯 픽셀 좌표계 <-> 격자 좌표계 변환에 쓰는 셀 크기/오프셋을 한 곳에서 계산.
    QRectF gridRect() const;
    double cellSize() const;
    QPointF cellCenter(int gridX, int gridY) const;
    // 클릭 좌표에서 가장 가까운 전광판을 찾는다. 반경 밖이면 -1.
    int hitTestDisplay(const QPointF &pos) const;

    int gridSize = 60; // setMapData() 전 임시 기본값일 뿐 — 실제 값은 항상 서버 응답으로 덮어씀
    QVector<QVector<int>> bitmap;
    QVector<FloorMapMarker> displays;
    QVector<FloorMapMarker> exits;
    QVector<FloorMapRoute> routes;

    int selectedDisplayId = -1;
    // 선택된 경로들(같은 전광판 -> 여러 출구 중 도달 가능한 것 전부)을 "그려지는 중" 애니메이션으로
    // 보여준다. 0.0=하나도 안 그려짐, 1.0=전체 다 그려짐.
    double revealProgress = 0.0;
    QTimer *revealTimer = nullptr;
};

#endif // FLOORMAPGRIDWIDGET_H
