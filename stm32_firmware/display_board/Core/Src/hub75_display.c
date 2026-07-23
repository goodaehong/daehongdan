#include "hub75_display.h"

/*
 * Seeengreat P3.0 64x64 HUB75 패널, 1/32 scan.
 * R1G1B1 = 위쪽 32행(row), R2G2B2 = 아래쪽 32행(row+32)을 같은 컬럼 시프트에 공유.
 * 그레이스케일(BCM) 없이 채널당 on/off만 지원하는 8색 버전.
 */

static uint8_t framebuffer[HUB75_HEIGHT][HUB75_WIDTH];

void HUB75_Init(void)
{
    HAL_GPIO_WritePin(HUB75_OE_GPIO_Port, HUB75_OE_Pin, GPIO_PIN_SET);   /* OE는 active-low: 시작은 출력 비활성 */
    HAL_GPIO_WritePin(HUB75_LAT_GPIO_Port, HUB75_LAT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HUB75_CLK_GPIO_Port, HUB75_CLK_Pin, GPIO_PIN_RESET);
    HUB75_Clear(HUB75_BLACK);
}

void HUB75_Clear(HUB75_Color color)
{
    for (uint8_t y = 0; y < HUB75_HEIGHT; y++)
    {
        for (uint8_t x = 0; x < HUB75_WIDTH; x++)
        {
            framebuffer[y][x] = (uint8_t)color;
        }
    }
}

void HUB75_SetPixel(uint8_t x, uint8_t y, HUB75_Color color)
{
    if (x >= HUB75_WIDTH || y >= HUB75_HEIGHT)
    {
        return;
    }
    framebuffer[y][x] = (uint8_t)color;
}

void HUB75_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, HUB75_Color color)
{
    uint16_t yEnd = (uint16_t)y + h;
    uint16_t xEnd = (uint16_t)x + w;

    for (uint16_t j = y; j < yEnd && j < HUB75_HEIGHT; j++)
    {
        for (uint16_t i = x; i < xEnd && i < HUB75_WIDTH; i++)
        {
            framebuffer[j][i] = (uint8_t)color;
        }
    }
}

void HUB75_DrawRectBorder(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t thickness, HUB75_Color color)
{
    HUB75_FillRect(x, y, w, thickness, color);                       /* top */
    HUB75_FillRect(x, y + h - thickness, w, thickness, color);        /* bottom */
    HUB75_FillRect(x, y, thickness, h, color);                        /* left */
    HUB75_FillRect(x + w - thickness, y, thickness, h, color);        /* right */
}

static inline void HUB75_ShiftColumn(uint8_t topColor, uint8_t bottomColor)
{
    uint32_t setMask = 0;
    uint32_t resetMask = 0;

    (topColor & 0x1) ? (setMask |= HUB75_R1_Pin) : (resetMask |= HUB75_R1_Pin);
    (topColor & 0x2) ? (setMask |= HUB75_G1_Pin) : (resetMask |= HUB75_G1_Pin);
    (topColor & 0x4) ? (setMask |= HUB75_B1_Pin) : (resetMask |= HUB75_B1_Pin);
    (bottomColor & 0x1) ? (setMask |= HUB75_R2_Pin) : (resetMask |= HUB75_R2_Pin);
    (bottomColor & 0x2) ? (setMask |= HUB75_G2_Pin) : (resetMask |= HUB75_G2_Pin);
    (bottomColor & 0x4) ? (setMask |= HUB75_B2_Pin) : (resetMask |= HUB75_B2_Pin);

    /* R1/G1/B1/R2/G2/B2가 GPIOB에 모여 있어 한 번의 BSRR 쓰기로 6비트 동시 갱신 */
    GPIOB->BSRR = setMask | (resetMask << 16);

    HAL_GPIO_WritePin(HUB75_CLK_GPIO_Port, HUB75_CLK_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(HUB75_CLK_GPIO_Port, HUB75_CLK_Pin, GPIO_PIN_RESET);
}

static inline void HUB75_SetAddress(uint8_t row)
{
    HAL_GPIO_WritePin(HUB75_A_GPIO_Port, HUB75_A_Pin, (row & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HUB75_B_GPIO_Port, HUB75_B_Pin, (row & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HUB75_C_GPIO_Port, HUB75_C_Pin, (row & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HUB75_D_GPIO_Port, HUB75_D_Pin, (row & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HUB75_E_GPIO_Port, HUB75_E_Pin, (row & 0x10) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void HUB75_RefreshOnce(void)
{
    for (uint8_t row = 0; row < 32; row++)
    {
        for (uint8_t col = 0; col < HUB75_WIDTH; col++)
        {
            HUB75_ShiftColumn(framebuffer[row][col], framebuffer[row + 32][col]);
        }

        HAL_GPIO_WritePin(HUB75_OE_GPIO_Port, HUB75_OE_Pin, GPIO_PIN_SET);    /* 래치/주소 전환 중 블랭킹 */
        HAL_GPIO_WritePin(HUB75_LAT_GPIO_Port, HUB75_LAT_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(HUB75_LAT_GPIO_Port, HUB75_LAT_Pin, GPIO_PIN_RESET);
        HUB75_SetAddress(row);
        HAL_GPIO_WritePin(HUB75_OE_GPIO_Port, HUB75_OE_Pin, GPIO_PIN_RESET); /* 출력 재활성화 */
    }
}