#include "shared_state.h"

// 공유 상태 실체. 선언은 shared_state.h, 정의는 여기 한 곳에만 둔다
DetectionState detState[4];
SensorState    sensorState;

Database g_db;
Link*    g_link = nullptr;
