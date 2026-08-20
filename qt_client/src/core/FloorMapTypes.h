#ifndef FLOORMAPTYPES_H
#define FLOORMAPTYPES_H

#include <QVector>
#include <QPoint>

// 서버 evac_map_tools(EvacPlanner)가 만드는 데이터와 1:1 대응 — 평면도 대피경로 연동 프로토콜
// 제안 문서 참고. 서버 프로토콜 확정 전이라 지금은 FloorMapPage가 로컬 예시 데이터로만 채운다.
// 좌표는 원본이 (y,x) 순서(0~61)지만, 여기 waypoints는 Qt 좌표계에 맞춰 QPoint(x,y)로 보관한다.
struct FloorMapMarker {
    int id = 0;
    int y = 0;
    int x = 0;
};

struct FloorMapRoute {
    int displayId = 0;
    int exitId = 0;
    QVector<QPoint> waypoints; // QPoint(x, y), 꺾이는 지점만(서버 스펙)
};

#endif // FLOORMAPTYPES_H
