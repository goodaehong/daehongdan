#ifndef HUB75_DISPLAY_H
#define HUB75_DISPLAY_H

#include "main.h"
#include <stdint.h>

#define HUB75_WIDTH  64
#define HUB75_HEIGHT 64

/* 3비트 색상: bit0=R, bit1=G, bit2=B (그레이스케일 없음, 8색 고정) */
typedef enum {
    HUB75_BLACK   = 0x0,
    HUB75_RED     = 0x1,
    HUB75_GREEN   = 0x2,
    HUB75_YELLOW  = 0x3,
    HUB75_BLUE    = 0x4,
    HUB75_MAGENTA = 0x5,
    HUB75_CYAN    = 0x6,
    HUB75_WHITE   = 0x7,
} HUB75_Color;

void HUB75_Init(void);
void HUB75_Clear(HUB75_Color color);
void HUB75_SetPixel(uint8_t x, uint8_t y, HUB75_Color color);
void HUB75_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, HUB75_Color color);
void HUB75_DrawRectBorder(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t thickness, HUB75_Color color);

/* 0~100(%). OE를 짧게 켰다 끄는 비율로 전체 밝기를 조절함 (그레이스케일과는 무관, 전역 밝기만) */
void HUB75_SetBrightness(uint8_t percent);

/* 32개 주소 행을 1회 스캔. 화면 유지를 위해 메인 루프에서 계속 호출해야 함 */
void HUB75_RefreshOnce(void);

#endif /* HUB75_DISPLAY_H */
