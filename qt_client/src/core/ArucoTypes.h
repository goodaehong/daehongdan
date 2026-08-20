#ifndef ARUCOTYPES_H
#define ARUCOTYPES_H

#include <QVector>
#include <QString>

// 서버 aruco_board_config.txt의 MARKER 한 줄과 1:1 대응. 방향(rotationDeg)은 항상 0으로
// 고정 전송한다 — OpenCV가 90도 단위 자동 판별을 이미 하고 있어 Qt가 값을 받을 필요가 없음
// (재환님 확정안 #6).
struct ArucoMarkerInput {
    int id = 0;
    double x = 0, y = 0;      // 공장 실제 좌표계, 미터
    double sizeM = 0.04;      // 인쇄된 검은 정사각형 한 변, 미터
};

// query target=aruco_config 응답 / set_aruco_config 요청 본문과 1:1 대응.
struct ArucoChannelConfig {
    bool configured = false;
    double factoryMinX = 0, factoryMinY = 0, factoryMaxX = 60, factoryMaxY = 60;
    double modelScale = 50;
    double boardMinX = 0, boardMinY = 0, boardMaxX = 0, boardMaxY = 0;
    QVector<ArucoMarkerInput> markers;
};

// query target=aruco_status의 channels[] 원소 하나. v1은 마커 "개수"만 —
// 어떤 ID가 검출됐는지는 GridCoordinateMapper 확장이 필요해 시연 이후로 배치됨.
struct ArucoChannelStatus {
    int channel = 0;
    bool configured = false;
    int markerCount = 0;
    bool homographyExists = false;
    bool calibrating = false;
    bool hasLastResult = false;
    bool lastOk = false;
    QString lastReason;
    int lastAcceptedMarkers = 0;
    int lastDetectedMarkers = 0;
    double lastRmsPx = 0;
};

#endif // ARUCOTYPES_H
