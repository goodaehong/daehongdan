#ifndef DETECTIONTYPES_H
#define DETECTIONTYPES_H

#include <QString>
#include <QPolygonF>

// 계약① 감지결과의 boxes[] 원소 하나. network(ServerLink)와 widgets(VideoWidget) 양쪽에서 공유.
struct DetectionBox {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    QString cls;   // "FIRE" / "SMOKE"
    double score = 0.0;
};

// 감시 제외(ROI) 영역 하나. set_ignore_regions 메시지의 regions[] 원소와 1:1로 대응한다.
//
// 서버는 화재/연기를 서로 다른 감지 엔진으로 돌리고 각 엔진이 ROI를 따로 받는다(대홍님 회신).
// 그래서 영역마다 어느 엔진에 적용할지 구분이 필요하다 — 수증기 배출구는 연기 오탐만, 주황색
// 경광등은 화재 오탐만 나므로, 하나로 묶으면 오탐을 줄이려다 반대쪽 미검출을 만들게 된다.
struct RoiRegion {
    QPolygonF points;         // 꼭짓점 4개, 0~1 정규화 (zoom=1.0/pan=0 기준)
    QString label;            // "3번 라인 경광등" 등. 나중에 이 영역을 지워도 되는지 판단하는 근거
    bool applyFire = true;    // applyTo에 "fire" 포함 여부
    bool applySmoke = true;   // applyTo에 "smoke" 포함 여부
    bool enabled = true;      // 삭제하지 않고 잠시 끄기
};

#endif // DETECTIONTYPES_H
