#ifndef DETECTIONTYPES_H
#define DETECTIONTYPES_H

#include <QString>

// 계약① 감지결과의 boxes[] 원소 하나. network(ServerLink)와 widgets(VideoWidget) 양쪽에서 공유.
struct DetectionBox {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    QString cls;   // "FIRE" / "SMOKE"
    double score = 0.0;
};

#endif // DETECTIONTYPES_H
