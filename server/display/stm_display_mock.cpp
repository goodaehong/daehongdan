#include "stm_display.h"

// 광렬님 실구현 나오기 전 mock. 아무것도 안 하고 성공만 리턴
bool StmDisplay_Open(const char* devPath) { (void)devPath; return true; }
void StmDisplay_Close() {}
bool StmDisplay_SendUpdate(const DisplayUpdate& u) {
    (void)u; return true;
}
bool StmDisplay_SendAlert(DisplayDisaster type, int zoneId) {
    (void)type; (void)zoneId; return true;
}
bool StmDisplay_SendClear() { return true; }
bool StmDisplay_GetLinkOk() { return true; }   // Mock은 항상 응답함 취급
bool StmDisplay_SendEvacPath(uint8_t routeIndex,
                              const uint8_t* waypointsXY, uint8_t waypointCount) {
    (void)routeIndex; (void)waypointsXY; (void)waypointCount;
    return true;
}
bool StmDisplay_SendEvacFires(const uint8_t* firesXYR, uint8_t fireCount) {
    (void)firesXYR; (void)fireCount;
    return true;
}