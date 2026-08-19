#include "FloorMapGridWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QTimer>
#include <QtMath>

namespace {
const QColor kFreeCell("#1a1a26");
const QColor kWallCell("#3a3550");
const QColor kDisplayColor("#fb923c");
const QColor kExitColor("#34d399");
const QColor kRouteColor("#8b7cf6");
const QColor kSelectedDisplayColor("#f5f5fa");
}

FloorMapGridWidget::FloorMapGridWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 320);
    setMouseTracking(false);

    revealTimer = new QTimer(this);
    connect(revealTimer, &QTimer::timeout, this, [this]() {
        // 600ms 정도에 걸쳐 선택된 경로가 "그려지는 중"처럼 보이게 한다(요청: 경로가 움직이면서 뜨는 방식).
        revealProgress = qMin(1.0, revealProgress + 0.06);
        if (revealProgress >= 1.0)
            revealTimer->stop();
        update();
    });
}

void FloorMapGridWidget::setMapData(int newGridSize, const QVector<QVector<int>> &newBitmap,
                                     const QVector<FloorMapMarker> &newDisplays,
                                     const QVector<FloorMapMarker> &newExits,
                                     const QVector<FloorMapRoute> &newRoutes)
{
    gridSize = newGridSize;
    bitmap = newBitmap;
    displays = newDisplays;
    exits = newExits;
    routes = newRoutes;
    selectedDisplayId = -1;
    revealProgress = 0.0;
    revealTimer->stop();
    update();
}

void FloorMapGridWidget::clearMapData()
{
    bitmap.clear();
    displays.clear();
    exits.clear();
    routes.clear();
    selectedDisplayId = -1;
    update();
}

QRectF FloorMapGridWidget::gridRect() const
{
    // 정사각형 격자를 위젯 안에 여백 없이 최대 크기로, 가운데 정렬.
    const double side = qMin(width(), height());
    return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
}

double FloorMapGridWidget::cellSize() const
{
    if (gridSize <= 0)
        return 0.0;
    return gridRect().width() / gridSize;
}

QPointF FloorMapGridWidget::cellCenter(int gridX, int gridY) const
{
    const QRectF r = gridRect();
    const double cs = cellSize();
    return QPointF(r.left() + (gridX + 0.5) * cs, r.top() + (gridY + 0.5) * cs);
}

int FloorMapGridWidget::hitTestDisplay(const QPointF &pos) const
{
    const double cs = cellSize();
    if (cs <= 0)
        return -1;
    // 셀 하나 정도 반경까지는 클릭으로 인정 — 62x62 격자에서 정확히 점 하나만 누르긴 어렵다.
    const double radius = qMax(10.0, cs * 1.5);
    int best = -1;
    double bestDist = radius;
    for (const FloorMapMarker &d : displays) {
        const QPointF c = cellCenter(d.x, d.y);
        const double dist = QLineF(pos, c).length();
        if (dist <= bestDist) {
            bestDist = dist;
            best = d.id;
        }
    }
    return best;
}

void FloorMapGridWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || bitmap.isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int id = hitTestDisplay(event->position());
    if (id >= 0) {
        selectedDisplayId = id;
        revealProgress = 0.0;
        revealTimer->start(16);
        update();
        emit displayClicked(id);
        return;
    }
    QWidget::mousePressEvent(event);
}

void FloorMapGridWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#0d0d16"));

    if (bitmap.isEmpty() || gridSize <= 0)
        return;

    const QRectF r = gridRect();
    const double cs = cellSize();

    // 셀 하나씩 그리는 대신 장애물 셀만 한 덩어리 QPainterPath로 모아서 그린다(62x62=3844칸이라
    // drawRect를 낱개로 부르면 불필요하게 많음 — path 하나로 합쳐 draw 호출 수를 줄인다).
    painter.fillRect(r, kFreeCell);
    QPainterPath wallPath;
    for (int y = 0; y < gridSize && y < bitmap.size(); ++y) {
        const QVector<int> &row = bitmap[y];
        for (int x = 0; x < gridSize && x < row.size(); ++x) {
            if (row[x] != 0)
                wallPath.addRect(QRectF(r.left() + x * cs, r.top() + y * cs, cs + 0.5, cs + 0.5));
        }
    }
    painter.fillPath(wallPath, kWallCell);

    painter.setPen(QPen(QColor(0, 0, 0, 60), 1));
    painter.drawRect(r);

    // 선택된 전광판의 경로들 — revealProgress만큼만 그려서 "그려지는 중" 느낌을 준다.
    if (selectedDisplayId >= 0) {
        for (const FloorMapRoute &route : routes) {
            if (route.displayId != selectedDisplayId || route.waypoints.size() < 2)
                continue;

            double totalLen = 0.0;
            QVector<double> segLens;
            for (int i = 1; i < route.waypoints.size(); ++i) {
                const QPointF a = cellCenter(route.waypoints[i - 1].x(), route.waypoints[i - 1].y());
                const QPointF b = cellCenter(route.waypoints[i].x(), route.waypoints[i].y());
                const double len = QLineF(a, b).length();
                segLens.append(len);
                totalLen += len;
            }
            double remaining = totalLen * revealProgress;

            QPainterPath path;
            path.moveTo(cellCenter(route.waypoints[0].x(), route.waypoints[0].y()));
            for (int i = 1; i < route.waypoints.size(); ++i) {
                const QPointF a = cellCenter(route.waypoints[i - 1].x(), route.waypoints[i - 1].y());
                const QPointF b = cellCenter(route.waypoints[i].x(), route.waypoints[i].y());
                const double len = segLens[i - 1];
                if (remaining >= len) {
                    path.lineTo(b);
                    remaining -= len;
                } else {
                    const double t = len > 0 ? remaining / len : 0;
                    path.lineTo(a + (b - a) * t);
                    remaining = 0;
                    break;
                }
            }
            painter.setPen(QPen(kRouteColor, qMax(2.0, cs * 0.35), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(path);
        }
    }

    // 출구 마커
    painter.setPen(Qt::NoPen);
    for (const FloorMapMarker &e : exits) {
        painter.setBrush(kExitColor);
        painter.drawEllipse(cellCenter(e.x, e.y), cs * 0.55, cs * 0.55);
    }

    // 전광판 마커 — 선택된 것만 흰 테두리로 강조.
    for (const FloorMapMarker &d : displays) {
        const QPointF c = cellCenter(d.x, d.y);
        painter.setBrush(kDisplayColor);
        painter.setPen(d.id == selectedDisplayId ? QPen(kSelectedDisplayColor, 2) : Qt::NoPen);
        painter.drawEllipse(c, cs * 0.7, cs * 0.7);
    }
}
