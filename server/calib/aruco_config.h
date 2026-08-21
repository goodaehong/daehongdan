#pragma once
#include <string>

// Qt가 보낸 좌표 값으로 aruco_board_config.txt 를 만든다.
// 다른 채널의 BOARD/MARKER 줄은 그대로 보존하고, 저장 전 백업을 남긴다.
// 좌표가 바뀌면 기존 변환표(homography)는 무효라 함께 치운다.
// 실패 시 false + reason (Qt에 그대로 보여줄 한글 사유)
bool ArucoConfig_Apply(const std::string& line, int* chOut, std::string* reason);

// 저장된 좌표를 Qt 폼에 되돌려준다. 재보정할 때 처음부터 다시 입력하지 않게.
// 해당 채널 설정이 없으면 빈 문자열
std::string ArucoConfig_ToJson(int ch);

// 보정 계산 스크립트를 백그라운드로 실행한다.
// 계산이 수십 초 걸려서 여기서 기다리면 통신 스레드가 막힌다 —
// 접수만 하고 true, 끝나면 등록해둔 콜백으로 결과를 알린다.
// 이미 그 채널이 계산 중이면 false + reason
bool ArucoConfig_RunCalibration(int ch, std::string* reason);

// 계산 중단 요청. 마커를 잘못 놓은 걸 도중에 알아챘을 때 쓴다.
// 계산 중이 아니면 false + reason
bool ArucoConfig_CancelCalibration(int ch, std::string* reason);

// 이 채널이 지금 계산 중인가. calib_status 응답에 실어 Qt가 표시한다
bool ArucoConfig_IsRunning(int ch);

// 계산 결과 (계산 스레드에서 호출)
enum class CalibRunResult { Ok, Error, Cancelled, Timeout };
using CalibRunDoneFn = void (*)(int ch, CalibRunResult r, const std::string& detail);
void ArucoConfig_SetOnDone(CalibRunDoneFn fn);

// 서버 종료 시 계산 스레드 정리 (돌고 있으면 중단시킨다)
void ArucoConfig_Shutdown();