#include "stm_display.h"

// 광렬님 실구현 나오기 전 mock. 아무것도 안 하고 성공만 리턴
bool StmDisplay_Open(const char* devPath) { (void)devPath; return true; }
void StmDisplay_Close() {}
bool StmDisplay_SendUpdate(const SensorReading& s, const std::string& state) {
    (void)s; (void)state; return true;
}
bool StmDisplay_SendAlert(const std::string& cause, int zoneId) {
    (void)cause; (void)zoneId; return true;
}
bool StmDisplay_SendClear() { return true; }
bool StmDisplay_GetLinkOk() { return true; }   // Mock은 항상 응답함 취급