#ifndef ARUCOTYPES_H
#define ARUCOTYPES_H

#include <QString>

// query target=calib_status 응답의 channels[] 원소 1:1 대응 (server/calib/calib_store.cpp).
// 좌표 설정(aruco_board_config.txt 등)은 Qt가 아니라 SSH로 여전히 수동 작성한다(PR #65 기준) —
// Qt는 상태 조회 + 재로드 요청만 담당한다.
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
};

#endif // ARUCOTYPES_H
