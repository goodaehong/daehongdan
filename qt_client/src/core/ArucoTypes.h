#ifndef ARUCOTYPES_H
#define ARUCOTYPES_H

#include <QString>
#include <QVector>

// query target=calib_status 응답의 channels[] 원소 1:1 대응 (server/calib/calib_store.cpp).
struct CalibChannelStatus {
    int channel = 0;
    QString stage;    // "미확인"/"미설정"/"렌즈 보정값 없음"/"보정 미완료"/"적용됨"
    bool ready = false;
    QString hint;     // 다음에 할 일 안내 — 그대로 표시
    QString detail;   // 감지 코어 원문(영어) — 기본은 접어둠

    // 좌표 계산기가 실제로 도는 채널에서만 채워진다(hasLive). 배치도가 없으면 세지도 않아서
    // 0/0과 "설정됐는데 마커 안 보임"을 구분하려고 서버가 아예 필드를 안 보낸다.
    bool hasLive = false;
    int detectedMarkers = 0;
    int acceptedMarkers = 0;
    double errorPx = 0;
    bool lensApplied = false;
    bool staticHomography = false;
    // 보정 계산이 지금 돌고 있는지(run_calibration 접수 후 calibration_done 오기 전까지 true).
    // true인 동안 "보정 실행" 버튼은 잠그고 "중단" 버튼을 보여준다.
    bool running = false;
};

// set_aruco_config의 markers[] 원소 하나 = ArUco 마커 한 장의 실측 배치 정보.
struct ArucoMarkerConfig {
    int id = 0;         // 0~49, 채널 안에서 중복 불가
    double x = 0;        // 마커 중심 X (m)
    double y = 0;        // 마커 중심 Y (m)
    double sizeCm = 0;   // 마커 한 변 길이 — cm로 주고받고 서버가 m로 변환해 저장
    double rotation = 0; // 도(degree)
};

// 채널 하나의 ArUco 좌표 설정 전체(set_aruco_config 송신 / query target=aruco_config 수신 공용).
struct ArucoChannelConfig {
    int channel = 0;
    // 공장 실측 범위(m) — 이 채널 카메라가 담당하는 물리적 영역.
    double factoryMinX = 0, factoryMinY = 0, factoryMaxX = 0, factoryMaxY = 0;
    double modelScale = 0;   // 모형 축척(예: 실제 50cm = 모형 1cm 이면 50)
    // board는 factory 범위 중 이 채널이 실제로 담당하는 부분 범위(m).
    double boardMinX = 0, boardMinY = 0, boardMaxX = 0, boardMaxY = 0;
    QVector<ArucoMarkerConfig> markers;
};

#endif // ARUCOTYPES_H
