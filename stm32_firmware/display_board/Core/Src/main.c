/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "hub75_display.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
extern const uint8_t HUB75_Alphabet[9];
extern const uint16_t HUB75_Korean1[10];
extern const uint16_t HUB75_Korean2[10];
extern const uint16_t HUB75_Shape[13];
extern const uint8_t HUB75_Shape2[8];
extern const uint8_t HUB75_Shape3[8];
extern const uint8_t HUB75_Shape4[8];
extern const uint8_t HUB75_Shape5[8];
extern const uint8_t HUB75_Number1[8];
extern const uint8_t HUB75_Number2[8];
extern const uint8_t HUB75_Number3[8];
extern const uint8_t HUB75_Number4[8];
extern const uint8_t HUB75_Number5[8];
extern const uint8_t HUB75_Number6[8];
extern const uint8_t HUB75_Number7[8];
extern const uint8_t HUB75_Number8[8];
extern const uint8_t HUB75_Number9[8];
extern const uint8_t HUB75_Number0[8];
extern const uint16_t HUB75_Korean3[9];
extern const uint16_t HUB75_Korean4[9];
extern const uint16_t HUB75_Shape6[10];
extern const uint16_t HUB75_Shape7[11];
extern const uint32_t HUB75_Shape8[21];
extern const uint8_t HUB75_Shape9[8];
extern const uint8_t HUB75_Shape10[8];
extern const uint8_t HUB75_TinyNumber0[8];
extern const uint8_t HUB75_TinyNumber1[8];
extern const uint8_t HUB75_TinyNumber2[8];
extern const uint8_t HUB75_TinyNumber3[8];
extern const uint8_t HUB75_TinyNumber4[8];
extern const uint8_t HUB75_TinyNumber5[8];
extern const uint8_t HUB75_TinyNumber6[8];
extern const uint8_t HUB75_TinyNumber7[8];
extern const uint8_t HUB75_TinyNumber8[8];
extern const uint8_t HUB75_TinyNumber9[8];
extern const uint32_t HUB75_Shape11[21];
extern const uint32_t HUB75_Shape12[21];
extern const uint64_t HUB75_Shape13[64];
extern const uint16_t HUB75_Korean5[12];
extern const uint16_t HUB75_Korean6[12];
extern const uint16_t HUB75_Korean7[12];
extern const uint16_t HUB75_Korean8[12];
extern const uint16_t HUB75_Korean9[12];
extern const uint16_t HUB75_Korean10[12];
extern const uint16_t HUB75_Korean11[12];
extern const uint16_t HUB75_Korean12[12];
extern const uint16_t HUB75_Korean13[12];
extern const uint16_t HUB75_Korean14[12];
extern const uint16_t HUB75_Korean15[12];
extern const uint16_t HUB75_Korean16[12];
extern const uint16_t HUB75_Korean17[12];
extern const uint16_t HUB75_Shape14[12];
extern const uint16_t HUB75_Alphabet1[12];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void DrawBitmap8(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t rowCount, HUB75_Color color)
{
  for (uint8_t row = 0; row < rowCount; row++)
  {
    uint8_t bits = bitmap[row];
    for (uint8_t col = 0; col < 8; col++)
    {
      if (bits & (0x80 >> col))
      {
        int16_t px = x + col;
        int16_t py = y + row;
        if (px >= 0 && px < HUB75_WIDTH && py >= 0 && py < HUB75_HEIGHT)
        {
          HUB75_SetPixel((uint8_t)px, (uint8_t)py, color);
        }
      }
    }
  }
}

static void DrawBitmap16(int16_t x, int16_t y, const uint16_t *bitmap, uint8_t rowCount, HUB75_Color color)
{
  for (uint8_t row = 0; row < rowCount; row++)
  {
    uint16_t bits = bitmap[row];
    for (uint8_t col = 0; col < 16; col++)
    {
      if (bits & (0x8000 >> col))
      {
        int16_t px = x + col;
        int16_t py = y + row;
        if (px >= 0 && px < HUB75_WIDTH && py >= 0 && py < HUB75_HEIGHT)
        {
          HUB75_SetPixel((uint8_t)px, (uint8_t)py, color);
        }
      }
    }
  }
}
static void DrawBitmap32(int16_t x, int16_t y, const uint32_t *bitmap, uint8_t rowCount, HUB75_Color color)
{
  for (uint8_t row = 0; row < rowCount; row++)
  {
    uint32_t bits = bitmap[row];
    for (uint8_t col = 0; col < 32; col++)
    {
      if (bits & (0x80000000u >> col))
      {
        int16_t px = x + col;
        int16_t py = y + row;
        if (px >= 0 && px < HUB75_WIDTH && py >= 0 && py < HUB75_HEIGHT)
        {
          HUB75_SetPixel((uint8_t)px, (uint8_t)py, color);
        }
      }
    }
  }
}
static void DrawBitmap64(int16_t x, int16_t y, const uint64_t *bitmap, uint8_t rowCount, HUB75_Color color)
{
  for (uint8_t row = 0; row < rowCount; row++)
  {
    uint64_t bits = bitmap[row];
    for (uint8_t col = 0; col < 64; col++)
    {
      if (bits & (0x8000000000000000ULL >> col))
      {
        int16_t px = x + col;
        int16_t py = y + row;
        if (px >= 0 && px < HUB75_WIDTH && py >= 0 && py < HUB75_HEIGHT)
        {
          HUB75_SetPixel((uint8_t)px, (uint8_t)py, color);
        }
      }
    }
  }
}
// 표정 3종 3초마다 순환 테스트: 웃음(초록) -> 무표정(노랑) -> 찡그림(빨강)
static const uint32_t * const faceShapes[3] = { HUB75_Shape11, HUB75_Shape8, HUB75_Shape12 };
static const HUB75_Color faceColors[3] = { HUB75_GREEN, HUB75_YELLOW, HUB75_RED };

// zoneColor: "A구역" 글자 색 (가스 상태 색과 동일하게 맞춤) - "역"이 표정 영역과 겹쳐서 같이 다시 그려야 함
static void DrawFace(uint8_t index, HUB75_Color zoneColor)
{
  HUB75_FillRect(38, 2, 32, 21, HUB75_BLACK);   // 이전 표정 지우기
  DrawBitmap32(38, 2, faceShapes[index], 21, faceColors[index]);
  DrawBitmap16(26, 8, HUB75_Korean2, 10, zoneColor);   // "역" 글자가 지우는 영역과 겹쳐서 같이 다시 그림
}

// 가스 그래프 색상 (표정/상태 index 기준): 0=정상(초록), 1=주의(노랑), 2=위험(빨강)
static const HUB75_Color gasColors[3] = { HUB75_GREEN, HUB75_YELLOW, HUB75_RED };

// 가스 농도(ppm, LPG 기준)에 따라 채워지는 칸 수(1~11) 계산
// 0~100=1, 101~200=2, 201~400=3, 401~600=4, 601~800=5, 801~1000=6,
// 1001~1200=7, 1201~1400=8, 1401~1600=9, 1601~2000=10, 2001 이상=11(가득)
static uint8_t GasPpmToFilledRows(uint16_t ppm)
{
  if (ppm <= 100)  return 1;
  if (ppm <= 200)  return 2;
  if (ppm <= 400)  return 3;
  if (ppm <= 600)  return 4;
  if (ppm <= 800)  return 5;
  if (ppm <= 1000) return 6;
  if (ppm <= 1200) return 7;
  if (ppm <= 1400) return 8;
  if (ppm <= 1600) return 9;
  if (ppm <= 2000) return 10;
  return 11;
}

// 가스 그래프: 그래프/라벨("가스")/천·백의 자리 숫자를 전부 가스 상태 색(초록/노랑/빨강)으로 통일
static void DrawGasGraph(uint8_t colorIndex, uint16_t gasPpm)
{
  HUB75_FillRect(22, 23, 16, 11, HUB75_BLACK);   // 이전 채움 지우기

  uint8_t filledRows = GasPpmToFilledRows(gasPpm);
  uint8_t whiteRows = 11 - filledRows;

  if (whiteRows > 0)
  {
    DrawBitmap16(22, 23, HUB75_Shape7, whiteRows, HUB75_WHITE);           // 위쪽 나머지 줄은 흰색
  }
  DrawBitmap16(22, 23 + whiteRows, HUB75_Shape7, filledRows, gasColors[colorIndex]); // 아래쪽 채워진 줄

  // 지우는 영역과 겹치는 요소들 다시 그림 (라벨/숫자 모두 가스 상태 색과 동일)
  DrawBitmap16(2, 25, HUB75_Korean3, 9, gasColors[colorIndex]);    // 가
  DrawBitmap16(11, 25, HUB75_Korean4, 9, gasColors[colorIndex]);   // 스
  DrawBitmap8(29, 25, HUB75_Number1, 8, gasColors[colorIndex]);    // 가스 농도 천의 자리
  DrawBitmap8(34, 25, HUB75_Number6, 8, gasColors[colorIndex]);    // 가스 농도 백의 자리
}

static uint8_t g_inAlertScreen = 0;  /* 전환 화면 표시 중이면 평상시 갱신 패킷 무시 */

// 위험 대피 전환 화면 (CMD 0x90 수신 시 표시)
// disasterType: 0 = A구역 화재발생!, 1 = A구역 가스유출! (둘 다 RED)
static HUB75_Color AlertScreenColor(uint8_t disasterType)
{
  (void)disasterType;
  return HUB75_RED;
}

static void DrawAlertScreen(uint8_t disasterType)
{
  HUB75_Color color = AlertScreenColor(disasterType);

  HUB75_Clear(HUB75_BLACK);
  DrawBitmap64(0, 0, HUB75_Shape13, 64, color);               // 테두리
  DrawBitmap16(13, 17, HUB75_Alphabet1, 12, color);           // A
  DrawBitmap16(25, 17, HUB75_Korean9, 12, color);             // 구
  DrawBitmap16(39, 17, HUB75_Korean10, 12, color);            // 역

  if (disasterType == 0)
  {
    DrawBitmap16(5, 33, HUB75_Korean5, 12, color);            // 화
    DrawBitmap16(18, 33, HUB75_Korean6, 12, color);           // 재
    DrawBitmap16(31, 33, HUB75_Korean7, 12, color);           // 발
    DrawBitmap16(44, 33, HUB75_Korean8, 12, color);           // 생
  }
  else
  {
    DrawBitmap16(5, 33, HUB75_Korean11, 12, color);           // 가
    DrawBitmap16(18, 33, HUB75_Korean12, 12, color);          // 스
    DrawBitmap16(31, 33, HUB75_Korean13, 12, color);          // 유
    DrawBitmap16(44, 33, HUB75_Korean14, 12, color);          // 출
  }

  DrawBitmap16(57, 33, HUB75_Shape14, 12, color);
}

// 대피도 격자 데이터 (server/evac_bitmap_export의 image_to_bitmap 도구로 평면도 이미지에서 생성)
// 60x60만 씀 - 맨 바깥 2px 링(x+2,y+2 오프셋 필요한 이유)은 위험 점멸 테두리 자리라 지도 데이터 없음
// 0=빈공간(통행가능), 1=장애물(벽/기계/구조물 전부 포함, 구분 없음) - 전광판/출구 위치는 별도 배열 참고
//
// ⚠️ 아래 BEGIN/END 사이는 image_to_bitmap 도구가 자동으로 통째로 덮어씀 - 손으로 고치지 말 것
/* USER CODE BEGIN EVAC_DATA */
// 0=빈공간(통행가능), 1=장애물(벽/기계/구조물 전부 포함, 구분 없음) - 전광판/출구 위치는 별도 배열(EvacDisplays/EvacExits) 참고
static const uint8_t HUB75_EvacMap[60][60] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,1,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,1,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1},
  {1,0,1,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1},
  {1,0,1,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,1},
  {1,0,1,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,1,0,0,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,1},
  {1,0,1,1,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,1},
  {1,0,1,1,1,1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,1},
  {1,0,1,1,1,1,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,1,1,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,1,1,1,1,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,1,0,1,1,1,0,0,1,1,0,0,1,1,0,0,0,1,1,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,1,0,1,1,1,1,0,0,0,0,1,1,1,1,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,1,0,1,1,1,1,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,1,1,1,1,0,0,1,1,1,1,0,1,1,1,1,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,1,1,1,1,1,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,1,1,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,1,1,1,1,1,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1},
};

static const uint8_t EvacDisplays[][2] = {
  {26,58},
  {58,37},
  {9,30},
  {9,21},
  {58,19},
  {24,1},
};
#define EVAC_DISPLAY_COUNT 6

static const uint8_t EvacExits[][2] = {
  {47,59},
  {0,47},
  {59,15},
  {21,0},
};
#define EVAC_EXIT_COUNT 4

/* USER CODE END EVAC_DATA */

#define EVAC_PATH_MAX_WAYPOINTS 30   /* CMD_EVAC_PATH 웨이포인트 최대 개수 */
#define EVAC_MAX_FIRES 6             /* CMD_EVAC_FIRES 화재 개수 최대값 (카메라 4채널 기준 여유) */
#define EVAC_PATH_CYCLE_MS 1300      /* 하이라이트가 출발점->도착점까지 흘러가는 데 걸리는 시간 */
#define EVAC_ANIM_REDRAW_MS 50       /* 애니메이션 프레임 갱신 주기 (~20fps) */
#define EVAC_PATH_GAP_SIZE 3         /* 경로 위를 흘러가는 자홍 하이라이트 픽셀 개수 */

/* CMD_EVAC_PATH로 받은 최신 대피경로(출구별로 하나씩, EvacExits와 같은 인덱스).
   waypointCount[r]==0이면 그 출구로 가는 경로 없음(미수신 또는 도달 불가) */
static uint8_t g_evacWaypoints[EVAC_EXIT_COUNT][EVAC_PATH_MAX_WAYPOINTS][2];
static uint8_t g_evacWaypointCount[EVAC_EXIT_COUNT];

/* CMD_EVAC_FIRES로 받은 최신 화재 위치 목록. g_evacFireCount==0이면 화재 없음 */
static uint8_t g_evacFireX[EVAC_MAX_FIRES], g_evacFireY[EVAC_MAX_FIRES], g_evacFireRadius[EVAC_MAX_FIRES];
static uint8_t g_evacFireCount = 0;
static uint32_t g_evacAnimCycleStart = 0;
static uint32_t g_evacAnimLastRedraw = 0;

// 경로(웨이포인트 배열) 전체의 이동 칸 수(스텝 수) 합산 - 애니메이션 진행률 계산용
static uint16_t EvacPathTotalSteps(const uint8_t wp[][2], uint8_t count)
{
  uint16_t total = 0;
  for (uint8_t i = 0; i + 1 < count; i++)
  {
    int16_t dx = (int16_t)wp[i + 1][0] - (int16_t)wp[i][0];
    int16_t dy = (int16_t)wp[i + 1][1] - (int16_t)wp[i][1];
    int16_t d = (dx != 0) ? dx : dy;   // 이동이 4방향뿐이라 dx/dy 중 하나는 항상 0
    total += (uint16_t)((d >= 0) ? d : -d);
  }
  return total;
}

// 경로를 시작점부터 revealSteps 칸만큼만 그림 (revealSteps에 총 길이를 넘기면 경로 전체가 그려짐)
static void DrawEvacPathPartial(const uint8_t wp[][2], uint8_t count, uint16_t revealSteps, HUB75_Color color)
{
  if (count == 0) return;
  HUB75_SetPixel(wp[0][0] + 2, wp[0][1] + 2, color);   // 시작점은 항상 켜짐

  uint16_t drawn = 0;
  for (uint8_t i = 0; i + 1 < count && drawn < revealSteps; i++)
  {
    int16_t x1 = wp[i][0], y1 = wp[i][1];
    int16_t dx = (int16_t)wp[i + 1][0] - x1;
    int16_t dy = (int16_t)wp[i + 1][1] - y1;
    int16_t segLen = (dx != 0) ? ((dx >= 0) ? dx : -dx) : ((dy >= 0) ? dy : -dy);
    int16_t stepX = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
    int16_t stepY = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;

    for (int16_t s = 1; s <= segLen && drawn < revealSteps; s++, drawn++)
    {
      HUB75_SetPixel((uint8_t)(x1 + s * stepX + 2), (uint8_t)(y1 + s * stepY + 2), color);
    }
  }
}

// 경로를 따라 stepIndex번째 칸(0=시작점, total=도착점)의 실좌표를 구함 - 움직이는 구멍 그릴 때 씀
static void EvacPathCellAt(const uint8_t wp[][2], uint8_t count, uint16_t stepIndex, uint8_t* outX, uint8_t* outY)
{
  uint16_t passed = 0;
  for (uint8_t i = 0; i + 1 < count; i++)
  {
    int16_t x1 = wp[i][0], y1 = wp[i][1];
    int16_t dx = (int16_t)wp[i + 1][0] - x1;
    int16_t dy = (int16_t)wp[i + 1][1] - y1;
    int16_t segLen = (dx != 0) ? ((dx >= 0) ? dx : -dx) : ((dy >= 0) ? dy : -dy);
    int16_t stepX = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
    int16_t stepY = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;

    if (stepIndex <= (uint16_t)(passed + segLen))
    {
      int16_t s = (int16_t)(stepIndex - passed);
      *outX = (uint8_t)(x1 + s * stepX);
      *outY = (uint8_t)(y1 + s * stepY);
      return;
    }
    passed = (uint16_t)(passed + segLen);
  }
  *outX = wp[count - 1][0];   // stepIndex가 범위를 넘으면 도착점 반환
  *outY = wp[count - 1][1];
}

// alertBorderVisible/alertDisasterType은 원래 UpdateAlertBorderBlink() 옆에 있었는데,
// DrawEvacuationScreen()이 테두리 점멸 상태를 그대로 따라 그리려고 참조하면서 여기로 옮겨옴
// (C는 변수를 쓰기 전에 선언이 먼저 나와야 함)
static uint8_t alertBorderVisible = 1;
static uint8_t alertDisasterType = 0;

// 대피도 화면: 벽(흰색)/전광판(노랑)/출구(초록, 3x3)/대피경로(빨간 바탕 + 흐르는 자홍 하이라이트)/화재(빨강+안쪽 노랑)를
// 60x60 격자에서 +2 오프셋으로 그림 (HUB75_Shape13 테두리 2px 안쪽에만 지도를 그리면 안 가려짐)
static void DrawEvacuationScreen(void)
{
  HUB75_Clear(HUB75_BLACK);

  for (uint8_t y = 0; y < 60; y++)
  {
    for (uint8_t x = 0; x < 60; x++)
    {
      if (HUB75_EvacMap[y][x])
      {
        HUB75_SetPixel(x + 2, y + 2, HUB75_WHITE);
      }
    }
  }

  for (uint8_t i = 0; i < EVAC_DISPLAY_COUNT; i++)
  {
    HUB75_SetPixel(EvacDisplays[i][0] + 2, EvacDisplays[i][1] + 2, HUB75_YELLOW);
  }

  // 대피경로: 경로 전체를 항상 빨간색으로 켜두고, 그 위에 자홍 하이라이트 3칸이 출발점->도착점을
  // 1.3초에 한 번씩 계속 흘러가는 애니메이션 - 도착하면 끊기지 않고 바로 출발점부터 다시 흐름.
  // 2단계로 나눠서 그림: ① 전체 경로(빨강) 먼저 다 그리고 ② 하이라이트(자홍)는 그 다음에 다 그림.
  // 순서가 중요함 - 경로끼리 출발점 근처를 공유하는 경우가 많은데, 한 루프 안에서
  // "그리고-하이라이트"를 경로별로 번갈아 하면 뒤에 그려지는 경로의 빨간칠이 앞 경로의
  // 하이라이트를 덮어써버려서, 공유 구간을 벗어나야 하이라이트가 보이는(즉 중간부터 시작하는
  // 것처럼 보이는) 버그가 생김. 그리기 다 끝난 뒤에 하이라이트를 얹으면 이 문제가 없음.
  uint32_t cyclePos = (HAL_GetTick() - g_evacAnimCycleStart) % EVAC_PATH_CYCLE_MS;

  for (uint8_t r = 0; r < EVAC_EXIT_COUNT; r++)
  {
    uint8_t count = g_evacWaypointCount[r];
    if (count == 0) continue;   /* 이 출구로 가는 경로 아직 안 받았거나 도달 불가 */

    uint16_t total = EvacPathTotalSteps(g_evacWaypoints[r], count);
    DrawEvacPathPartial(g_evacWaypoints[r], count, total, HUB75_RED);   // ① 경로 전체를 먼저 다 그림
  }

  for (uint8_t r = 0; r < EVAC_EXIT_COUNT; r++)
  {
    uint8_t count = g_evacWaypointCount[r];
    if (count == 0) continue;

    uint16_t total = EvacPathTotalSteps(g_evacWaypoints[r], count);
    if (total == 0) continue;   /* 웨이포인트 1개뿐(시작=도착) - 흐르는 하이라이트 없음 */

    uint16_t gapStart = (uint16_t)(((uint32_t)cyclePos * total) / EVAC_PATH_CYCLE_MS);
    for (uint8_t k = 0; k < EVAC_PATH_GAP_SIZE; k++)   // ② 하이라이트는 전체 다 그려진 뒤에 얹음
    {
      uint16_t idx = gapStart + k;
      if (idx > total) idx = total;   /* 도착점을 넘어가면 도착점에 고정 */
      uint8_t cx, cy;
      EvacPathCellAt(g_evacWaypoints[r], count, idx, &cx, &cy);
      HUB75_SetPixel((uint8_t)(cx + 2), (uint8_t)(cy + 2), HUB75_MAGENTA);
    }
  }

  // 화재 위치: g_evacFireCount개 전부, 각자 반경만큼 빨간 사각형으로 채우고 그 안쪽에 더 작은
  // 노란 사각형을 얹어서 불처럼 보이게 함 (바깥 빨강 = 불꽃, 안쪽 노랑 = 불의 중심).
  // 안쪽 노란 정사각형 한 변 = r+1 (r=1/한변3이면 안쪽 한변2, r=2/한변5면 안쪽 한변3 - 요구사항 예시 기준).
  // r=0(반경 없음, 1픽셀 화재)은 너무 작아서 두 색을 구분해 봐야 의미 없으므로 빨강 단색으로 둠.
  for (uint8_t f = 0; f < g_evacFireCount; f++)
  {
    int16_t fx = g_evacFireX[f], fy = g_evacFireY[f], r = g_evacFireRadius[f];
    for (int16_t dy = -r; dy <= r; dy++)
    {
      for (int16_t dx = -r; dx <= r; dx++)
      {
        int16_t x = fx + dx, y = fy + dy;
        if (x >= 0 && x < 60 && y >= 0 && y < 60)
        {
          HUB75_SetPixel((uint8_t)(x + 2), (uint8_t)(y + 2), HUB75_RED);
        }
      }
    }

    if (r >= 1)
    {
      int16_t inner = r + 1;                 /* 안쪽 노란 정사각형 한 변 길이 */
      int16_t lo = -((inner - 1) / 2);        /* 홀/짝 상관없이 중심에 최대한 맞춘 시작 오프셋 */
      int16_t hi = lo + inner - 1;
      for (int16_t dy = lo; dy <= hi; dy++)
      {
        for (int16_t dx = lo; dx <= hi; dx++)
        {
          int16_t x = fx + dx, y = fy + dy;
          if (x >= 0 && x < 60 && y >= 0 && y < 60)
          {
            HUB75_SetPixel((uint8_t)(x + 2), (uint8_t)(y + 2), HUB75_YELLOW);
          }
        }
      }
    }
  }

  // 출구: 한 픽셀만 있으면 잘 안 보여서 상하좌우/대각선까지 3x3(9픽셀)로 채움.
  // 대피경로(빨강)가 그려진 뒤에 마지막으로 얹어서, 경로가 출구 칸을 지나가도 출구가 항상 보이게 함.
  // 출구는 항상 맨 바깥 경계선(x/y == 0 또는 59) 위에 있어서 3x3의 바깥쪽 줄은 테두리(2px) 영역까지
  // 살짝 넘어가는데, 그 부분은 점멸하는 경고 테두리가 마지막에 덮어 그려도 상관없음(요구사항 그대로).
  for (uint8_t i = 0; i < EVAC_EXIT_COUNT; i++)
  {
    int16_t ex = EvacExits[i][0], ey = EvacExits[i][1];
    for (int16_t dy = -1; dy <= 1; dy++)
    {
      for (int16_t dx = -1; dx <= 1; dx++)
      {
        HUB75_SetPixel((uint8_t)(ex + dx + 2), (uint8_t)(ey + dy + 2), HUB75_GREEN);
      }
    }
  }

  // 테두리: 점멸 상태(alertBorderVisible)를 그대로 따라 그림 - 이 함수가 애니메이션 때문에
  // 자주 다시 불려도 UpdateAlertBorderBlink()가 관리하는 점멸 상태랑 어긋나지 않게
  DrawBitmap64(0, 0, HUB75_Shape13, 64, alertBorderVisible ? AlertScreenColor(alertDisasterType) : HUB75_BLACK);
}

// 대피경로 애니메이션 프레임 갱신 (EVAC_ANIM_REDRAW_MS마다 한 번씩만 다시 그림)
static void UpdateEvacPathAnimation(void)
{
  uint32_t now = HAL_GetTick();
  if (now - g_evacAnimLastRedraw < EVAC_ANIM_REDRAW_MS) return;
  g_evacAnimLastRedraw = now;
  DrawEvacuationScreen();
}

// 테두리 점멸: 0.1초 간격으로 계속 깜빡임. CMD 0xA0(비상 해제)을 받기 전까지는
// 계속 위험 화면(전환->대피도) 유지 - 시간이 지났다고 자동으로 평상시 복귀하지 않음
// (위험이 오래 이어져도 화면이 멋대로 "괜찮음"으로 안 바뀌게 하려는 것)
#define ALERT_BLINK_INTERVAL_MS  100
#define ALERT_TO_EVACUATION_MS   3000   /* 전환 화면 표시 후 대피도 화면으로 자동 전환까지 걸리는 시간 */

static uint32_t alertBlinkCycleStart = 0;   /* 전환 화면 진입 시각(ms) - 점멸 + 3초 전환 기준 */
static uint8_t alertShowingEvacuation = 0;  /* 0=전환 화면, 1=대피도 화면 */

static void UpdateAlertBorderBlink(void)
{
  uint32_t elapsed = HAL_GetTick() - alertBlinkCycleStart;

  if (!alertShowingEvacuation && elapsed >= ALERT_TO_EVACUATION_MS)   // 3초 경과 -> 대피도 화면으로 전환
  {
    g_evacAnimCycleStart = HAL_GetTick();   /* 대피경로 애니메이션 이번 진입에서 처음부터 다시 시작 */
    DrawEvacuationScreen();
    alertShowingEvacuation = 1;
    alertBorderVisible = 1;   /* 방금 테두리 포함해서 다시 그렸으니 "보이는 상태"로 동기화 */
  }

  uint8_t shouldBeVisible = ((elapsed / ALERT_BLINK_INTERVAL_MS) % 2) == 0;

  if (shouldBeVisible != alertBorderVisible)
  {
    alertBorderVisible = shouldBeVisible;
    DrawBitmap64(0, 0, HUB75_Shape13, 64, alertBorderVisible ? AlertScreenColor(alertDisasterType) : HUB75_BLACK);
  }
}

// 좌표는 (1,1)~(64,64) 기준표를 0-index로 변환(-1)해서 배치.
// 시:분 콜론(HUB75_Shape10) 1초 주기 깜빡임 (0.5초 켜짐 + 0.5초 꺼짐)
static uint8_t colonVisible = 1;

static void UpdateColonBlink(void)
{
  uint8_t shouldBeVisible = ((HAL_GetTick() / 500) % 2) == 0;
  if (shouldBeVisible != colonVisible)
  {
    colonVisible = shouldBeVisible;
    DrawBitmap8(51, 54, HUB75_Shape10, 8, colonVisible ? HUB75_WHITE : HUB75_BLACK);
  }
}

static void DrawStaticScene(void)
{
  HUB75_Clear(HUB75_BLACK);   // 이전 화면(전환/대피도 등) 잔상 없이 항상 깨끗하게 시작
  DrawBitmap8(6, 8, HUB75_Alphabet, 9, HUB75_WHITE);           // A
  DrawBitmap16(16, 8, HUB75_Korean1, 10, HUB75_WHITE);         // 구
  DrawBitmap16(26, 8, HUB75_Korean2, 10, HUB75_WHITE);         // 역
  DrawFace(0, HUB75_WHITE);                                    // 표정 (첫 프레임: 웃음)

  colonVisible = 1;                                            // 화면 새로 그릴 땐 항상 켜진 상태로 시작
  DrawBitmap8(11, 53, HUB75_Shape9, 8, HUB75_WHITE);           // .
  DrawBitmap8(23, 53, HUB75_Shape9, 8, HUB75_WHITE);           // .
  DrawBitmap8(51, 54, HUB75_Shape10, 8, HUB75_WHITE);          // :

  DrawBitmap16(1, 38, HUB75_Shape, 13, HUB75_YELLOW);          // 온도(태양)
  DrawBitmap8(16, 41, HUB75_Number3, 8, HUB75_YELLOW);         // 온도 십의 자리
  DrawBitmap8(21, 41, HUB75_Number1, 8, HUB75_YELLOW);         // 온도 일의 자리
  DrawBitmap8(25, 41, HUB75_Shape3, 8, HUB75_YELLOW);          // 온도(섭씨)

  DrawBitmap16(35, 39, HUB75_Shape6, 10, HUB75_BLUE);          // 습도(물방울)
  DrawBitmap8(44, 41, HUB75_Number3, 8, HUB75_BLUE);           // 습도 십의 자리
  DrawBitmap8(49, 41, HUB75_Number5, 8, HUB75_BLUE);           // 습도 일의 자리
  DrawBitmap8(55, 41, HUB75_Shape2, 8, HUB75_BLUE);            // %

  DrawGasGraph(0, 0);                                          // 가스 그래프+라벨("가스")+천/백의 자리 (첫 프레임: 정상, ppm=0)
  DrawBitmap8(39, 25, HUB75_Number0, 8, gasColors[0]);         // 가스 농도 십의 자리
  DrawBitmap8(44, 25, HUB75_Number0, 8, gasColors[0]);         // 가스 농도 일의 자리
  DrawBitmap8(49, 26, HUB75_Shape4, 8, gasColors[0]);          // 가스 농도(pp)
  DrawBitmap8(57, 26, HUB75_Shape5, 8, gasColors[0]);          // 가스 농도(m)

  HUB75_FillRect(0, 36, 64, 1, HUB75_WHITE);                   // 37번 행 구분선 (흰색으로 꽉 채움)

  DrawBitmap8(2, 53, HUB75_TinyNumber2, 8, HUB75_WHITE);       // 연도 십의 자리
  DrawBitmap8(7, 53, HUB75_TinyNumber6, 8, HUB75_WHITE);       // 연도 일의 자리
  DrawBitmap8(14, 53, HUB75_TinyNumber0, 8, HUB75_WHITE);      // 월 십의 자리
  DrawBitmap8(19, 53, HUB75_TinyNumber0, 8, HUB75_WHITE);      // 월 일의 자리
  DrawBitmap8(26, 53, HUB75_TinyNumber0, 8, HUB75_WHITE);      // 일 십의 자리
  DrawBitmap8(31, 53, HUB75_TinyNumber0, 8, HUB75_WHITE);      // 일 일의 자리
  DrawBitmap8(41, 53, HUB75_TinyNumber0, 8, HUB75_WHITE);      // 시 십의 자리
  DrawBitmap8(46, 53, HUB75_TinyNumber0, 8, HUB75_WHITE);      // 시 일의 자리
  DrawBitmap8(53, 53, HUB75_TinyNumber0, 8, HUB75_WHITE);      // 분 십의 자리
  DrawBitmap8(58, 53, HUB75_TinyNumber0, 8, HUB75_WHITE);      // 분 일의 자리
}

/* ===================== Pi <-> STM32 UART 프로토콜 ===================== */
/* STX/ETX는 실제 팀 프로토콜 값으로 확정되면 여기만 바꾸면 됨 */
#define PACKET_STX     0xAA
#define PACKET_ETX     0x55
#define CMD_UPDATE     0x80   /* 평상시 화면 갱신 (Pi -> STM32) */
#define CMD_ALERT      0x90   /* 위험 대피 전환 (Pi -> STM32) */
#define CMD_ACK        0xB0   /* 전광판 상태 응답 (STM32 -> Pi) */
#define CMD_CLEAR      0xA0   /* 비상 해제, 평상시 화면 복귀 (Pi -> STM32) */
#define CMD_EVAC_PATH  0xB1   /* 대피경로 (Pi -> STM32) */
#define CMD_EVAC_FIRES 0xB2   /* 화재 위치 목록 (Pi -> STM32) */
#define UPDATE_DATA_LEN 11    /* 표정,가스색,가스H,가스L,온도,습도,시,분,년,월,일 */
#define ALERT_DATA_LEN  2     /* 재난종류,구역ID */
#define EVAC_PATH_MIN_LEN 2   /* routeIndex,waypointCount (웨이포인트는 그 뒤에 가변 길이) */
#define EVAC_FIRES_MIN_LEN 1  /* fireCount (화재 목록은 그 뒤에 가변 길이) */
/* EVAC_PATH_MAX_WAYPOINTS, EVAC_MAX_FIRES는 DrawEvacuationScreen() 쪽에서 더 먼저 쓰여서
   이 파일 앞부분(EVAC_DATA 근처)에 정의돼있음 - 여기서 또 정의하면 중복이라 안 함 */

static const uint8_t * const NumberDigits[10] = {
  HUB75_Number0, HUB75_Number1, HUB75_Number2, HUB75_Number3, HUB75_Number4,
  HUB75_Number5, HUB75_Number6, HUB75_Number7, HUB75_Number8, HUB75_Number9
};
static const uint8_t * const TinyNumberDigits[10] = {
  HUB75_TinyNumber0, HUB75_TinyNumber1, HUB75_TinyNumber2, HUB75_TinyNumber3, HUB75_TinyNumber4,
  HUB75_TinyNumber5, HUB75_TinyNumber6, HUB75_TinyNumber7, HUB75_TinyNumber8, HUB75_TinyNumber9
};

/* 인터럽트로 채워지는 수신 링버퍼 */
#define RX_RING_SIZE 64
static volatile uint8_t rxRing[RX_RING_SIZE];
static volatile uint16_t rxHead = 0;
static volatile uint16_t rxTail = 0;
static uint8_t rxByte;

/* IT 수신 중 오버런/프레이밍 에러 등이 나면 여기로 옴 -> 반드시 재무장해야 다음 바이트를 계속 받음 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    __HAL_UART_CLEAR_PEFLAG(huart);
    HAL_UART_Receive_IT(&huart2, &rxByte, 1);
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)
  {
    uint16_t next = (rxHead + 1) % RX_RING_SIZE;
    if (next != rxTail)   /* 링버퍼 꽉 찼으면 그냥 버림(오버런) */
    {
      rxRing[rxHead] = rxByte;
      rxHead = next;
    }
    HAL_UART_Receive_IT(&huart2, &rxByte, 1);   /* 다음 1바이트 계속 수신 대기 */
  }
}

/* 패킷 파서 상태 */
typedef enum { WAIT_STX, WAIT_LEN, WAIT_CMD, WAIT_DATA, WAIT_CHECKSUM, WAIT_ETX } ParseState;
static ParseState parseState = WAIT_STX;
static uint8_t packetLen, packetCmd, packetData[70], packetDataIdx;   /* CMD_EVAC_PATH 최대 데이터(2+30*2=62)를 담을 수 있게 */
static uint32_t lastRxTick = 0;   /* 마지막으로 바이트를 처리한 시각(ms) - 유휴 타임아웃 재동기화용 */

/* 파싱된 최신 값들 (표정/가스색은 서로 다른 상태라 별도 변수로 관리) */
static uint8_t g_face = 0;       /* 0=웃음,1=무표정,2=찡그림 */
static uint8_t g_gasColor = 0;   /* 0=정상,1=주의,2=위험 */
static uint16_t g_gas = 0;
static uint8_t g_temp = 0, g_humidity = 0, g_hour = 0, g_minute = 0;
static uint8_t g_year = 0, g_month = 0, g_day = 0;
static volatile uint8_t g_dataUpdated = 0;

static void SendAckPacket(uint8_t status)
{
  uint8_t packet[6];
  packet[0] = PACKET_STX;
  packet[1] = 1;              /* Data Length */
  packet[2] = CMD_ACK;
  packet[3] = status;
  packet[4] = (uint8_t)(packet[1] + packet[2] + packet[3]);  /* 단순 합산 체크섬 */
  packet[5] = PACKET_ETX;
  HAL_UART_Transmit(&huart2, packet, sizeof(packet), 100);
}

static void HandlePacket(uint8_t cmd, const uint8_t *data, uint8_t len)
{
  if (cmd == CMD_UPDATE && len >= UPDATE_DATA_LEN)
  {
    /* faceShapes/faceColors/gasColors는 전부 3칸짜리 배열 - 패킷이 손상돼서 범위 밖 값이
       들어오면 그대로 인덱스로 써서 HardFault 나던 것 방지 (체크섬은 단순 합산이라 약함) */
    g_face      = (data[0] <= 2) ? data[0] : 2;
    g_gasColor  = (data[1] <= 2) ? data[1] : 2;
    g_gas       = ((uint16_t)data[2] << 8) | data[3];
    g_temp      = data[4];
    g_humidity  = data[5];
    g_hour      = data[6];
    g_minute    = data[7];
    g_year      = data[8];
    g_month     = data[9];
    g_day       = data[10];
    g_dataUpdated = 1;
  }
  else if (cmd == CMD_ALERT && len >= ALERT_DATA_LEN)
  {
    uint8_t disasterType = (data[0] == 0x02) ? 1 : 0;   /* 0x01=화재->0, 0x02=가스->1 */
    /* data[1] = 구역 ID, 지금은 A구역 문구만 있어서 미사용 */
    alertDisasterType = disasterType;
    DrawAlertScreen(alertDisasterType);
    g_inAlertScreen = 1;
    alertBlinkCycleStart = HAL_GetTick();
    alertBorderVisible = 1;
    alertShowingEvacuation = 0;   /* 새 위험 진입이니 전환 화면부터 다시 시작 */
  }
  else if (cmd == CMD_CLEAR)
  {
    DrawStaticScene();     /* 평상시 화면으로 즉시 복귀 */
    g_inAlertScreen = 0;
  }
  else if (cmd == CMD_EVAC_PATH && len >= EVAC_PATH_MIN_LEN)
  {
    uint8_t routeIndex = data[0];

    if (routeIndex < EVAC_EXIT_COUNT)   /* 범위 밖 인덱스(손상된 패킷 등)는 조용히 무시 */
    {
      uint8_t count = data[1];
      if (count > EVAC_PATH_MAX_WAYPOINTS) count = EVAC_PATH_MAX_WAYPOINTS;   /* 손상된 패킷 방어 */
      if (len < (uint8_t)(EVAC_PATH_MIN_LEN + count * 2)) count = 0;          /* 데이터 길이 안 맞으면 무시 */

      for (uint8_t i = 0; i < count; i++)
      {
        g_evacWaypoints[routeIndex][i][0] = data[EVAC_PATH_MIN_LEN + i * 2];       /* x */
        g_evacWaypoints[routeIndex][i][1] = data[EVAC_PATH_MIN_LEN + i * 2 + 1];   /* y */
      }
      g_evacWaypointCount[routeIndex] = count;
    }

    /* 여기서 DrawEvacuationScreen()을 즉시 호출하지 않음 - 출구 4곳 패킷이 텀 없이 연달아
       올 때(SendAllEvacRoutes 등) 무거운 전체 재렌더를 패킷마다 돌리면, 그 처리 시간 동안
       수신 링버퍼(64바이트)가 넘쳐서 뒤에 오는 패킷이 깨지는 문제가 있었음.
       대신 UpdateEvacPathAnimation()이 50ms마다 알아서 다시 그려주므로 그걸로 충분함 */
  }
  else if (cmd == CMD_EVAC_FIRES && len >= EVAC_FIRES_MIN_LEN)
  {
    uint8_t count = data[0];
    if (count > EVAC_MAX_FIRES) count = EVAC_MAX_FIRES;                          /* 손상된 패킷 방어 */
    if (len < (uint8_t)(EVAC_FIRES_MIN_LEN + count * 3)) count = 0;              /* 데이터 길이 안 맞으면 무시 */

    for (uint8_t i = 0; i < count; i++)
    {
      g_evacFireX[i]      = data[EVAC_FIRES_MIN_LEN + i * 3];
      g_evacFireY[i]      = data[EVAC_FIRES_MIN_LEN + i * 3 + 1];
      g_evacFireRadius[i] = data[EVAC_FIRES_MIN_LEN + i * 3 + 2];
    }
    g_evacFireCount = count;

    /* CMD_EVAC_PATH와 같은 이유로 여기서도 즉시 재렌더 안 함 - UpdateEvacPathAnimation()이 처리 */
  }
}

/* 링버퍼에서 바이트 하나씩 꺼내 상태기계로 패킷 조립 */
static void ProcessRxByte(uint8_t b)
{
  static uint8_t checksumCalc;

  lastRxTick = HAL_GetTick();

  switch (parseState)
  {
    case WAIT_STX:
      if (b == PACKET_STX) { parseState = WAIT_LEN; }
      break;

    case WAIT_LEN:
      packetLen = b;
      packetDataIdx = 0;
      checksumCalc = b;
      parseState = WAIT_CMD;
      break;

    case WAIT_CMD:
      packetCmd = b;
      checksumCalc += b;
      parseState = (packetLen > 0) ? WAIT_DATA : WAIT_CHECKSUM;
      break;

    case WAIT_DATA:
      if (packetDataIdx < sizeof(packetData))
      {
        packetData[packetDataIdx++] = b;
      }
      checksumCalc += b;
      if (packetDataIdx >= packetLen) { parseState = WAIT_CHECKSUM; }
      break;

    case WAIT_CHECKSUM:
      parseState = (b == checksumCalc) ? WAIT_ETX : WAIT_STX;   /* 체크섬 틀리면 그냥 버리고 재동기화 */
      break;

    case WAIT_ETX:
      if (b == PACKET_ETX)
      {
        HandlePacket(packetCmd, packetData, packetLen);
      }
      parseState = WAIT_STX;
      break;

    default:
      parseState = WAIT_STX;
      break;
  }
}

static void PollUartRx(void)
{
  while (rxTail != rxHead)
  {
    uint8_t b = rxRing[rxTail];
    rxTail = (rxTail + 1) % RX_RING_SIZE;
    ProcessRxByte(b);
  }

  /* 패킷 중간에 바이트가 하나 깨져서 영영 정렬이 안 맞는 경우 대비:
     일정 시간 새 바이트가 없는데 아직 STX 대기 상태가 아니면 강제로 재동기화 */
  if (parseState != WAIT_STX && (HAL_GetTick() - lastRxTick) > 50)
  {
    parseState = WAIT_STX;
  }
}

static void UpdateDigit(int16_t x, int16_t y, uint8_t digit, HUB75_Color color, uint8_t tiny)
{
  HUB75_FillRect(x, y, 8, 8, HUB75_BLACK);
  DrawBitmap8(x, y, tiny ? TinyNumberDigits[digit] : NumberDigits[digit], 8, color);
}


/* 수신값을 실제 화면에 반영 */
static void UpdateDynamicDisplay(void)
{
  HUB75_Color gasColor = gasColors[g_gasColor];   // 가스 농도 숫자/단위는 그래프 색과 동일

  DrawBitmap8(6, 8, HUB75_Alphabet, 9, HUB75_WHITE);          // A
  DrawBitmap16(16, 8, HUB75_Korean1, 10, HUB75_WHITE);        // 구
  DrawFace(g_face, HUB75_WHITE);                              // "역"은 DrawFace 안에서 표정과 함께 다시 그림
  DrawGasGraph(g_gasColor, g_gas);

  uint16_t gasDisplay = (g_gas > 9999) ? 9999 : g_gas;
  UpdateDigit(29, 25, (uint8_t)((gasDisplay / 1000) % 10), gasColor, 0);
  UpdateDigit(34, 25, (uint8_t)((gasDisplay / 100) % 10), gasColor, 0);
  UpdateDigit(39, 25, (uint8_t)((gasDisplay / 10) % 10), gasColor, 0);
  UpdateDigit(44, 25, (uint8_t)(gasDisplay % 10), gasColor, 0);
  DrawBitmap8(49, 26, HUB75_Shape4, 8, gasColor);             // 가스 농도(pp)
  DrawBitmap8(57, 26, HUB75_Shape5, 8, gasColor);             // 가스 농도(m)

  /* 온도 */
  UpdateDigit(16, 41, (uint8_t)(g_temp / 10), HUB75_YELLOW, 0);
  UpdateDigit(21, 41, (uint8_t)(g_temp % 10), HUB75_YELLOW, 0);
  DrawBitmap8(25, 41, HUB75_Shape3, 8, HUB75_YELLOW);         // 온도(섭씨) - 일의 자리와 겹치는 부분 다시 그림

  /* 습도 */
  UpdateDigit(44, 41, (uint8_t)(g_humidity / 10), HUB75_BLUE, 0);
  UpdateDigit(49, 41, (uint8_t)(g_humidity % 10), HUB75_BLUE, 0);
  DrawBitmap8(55, 41, HUB75_Shape2, 8, HUB75_BLUE);           // % - 일의 자리와 겹치는 부분 다시 그림

  /* 시:분 */
  UpdateDigit(41, 53, (uint8_t)(g_hour / 10), HUB75_WHITE, 1);
  UpdateDigit(46, 53, (uint8_t)(g_hour % 10), HUB75_WHITE, 1);
  UpdateDigit(53, 53, (uint8_t)(g_minute / 10), HUB75_WHITE, 1);
  UpdateDigit(58, 53, (uint8_t)(g_minute % 10), HUB75_WHITE, 1);
  DrawBitmap8(51, 54, HUB75_Shape10, 8, colonVisible ? HUB75_WHITE : HUB75_BLACK);   // : - 분 십의 자리와 겹치는 부분 다시 그림 (깜빡임 상태 유지)

  /* 년/월/일 */
  UpdateDigit(2, 53, (uint8_t)(g_year / 10), HUB75_WHITE, 1);
  UpdateDigit(7, 53, (uint8_t)(g_year % 10), HUB75_WHITE, 1);
  UpdateDigit(14, 53, (uint8_t)(g_month / 10), HUB75_WHITE, 1);
  UpdateDigit(19, 53, (uint8_t)(g_month % 10), HUB75_WHITE, 1);
  UpdateDigit(26, 53, (uint8_t)(g_day / 10), HUB75_WHITE, 1);
  UpdateDigit(31, 53, (uint8_t)(g_day % 10), HUB75_WHITE, 1);
  DrawBitmap8(11, 53, HUB75_Shape9, 8, HUB75_WHITE);          // . - 월 십의 자리와 겹치는 부분 다시 그림
  DrawBitmap8(23, 53, HUB75_Shape9, 8, HUB75_WHITE);          // . - 일 십의 자리와 겹치는 부분 다시 그림
}

/* ===================== IWDG (하드웨어 워치독) ===================== */
/* HardFault 등으로 while(1) 루프가 멈추면 예전엔 전원을 다시 넣기 전까지 영원히
   안 돌아왔음 (stm32f4xx_it.c의 fault 핸들러들이 전부 빈 while(1)이라 복구 로직이 없음).
   이 프로젝트엔 HAL IWDG 드라이버(stm32f4xx_hal_iwdg.c)가 아예 없어서(hal_conf.h에서도
   비활성 상태) HAL 안 거치고 레지스터 직접 제어 - IWDG는 CMSIS 레벨이라 문제 없음.
   LSI(~32kHz, 메인 클럭과 무관하게 항상 도는 내부 저속 오실레이터)로 카운트다운하다가
   Kick 없이 0에 도달하면 하드웨어 리셋됨. PR=3(32분주)+RLR=2000 -> 약 2초
   (LSI 오차가 커서 실제로는 1.4~2.6초 사이일 수 있음, 그래도 몇 초 안에 복구된다는
   목적엔 충분) */
#define IWDG_RELOAD_VALUE 2000U   /* 약 2초 - LSI/32 기준 */

static void IWDG_Init(void)
{
  IWDG->KR = 0x5555;                 /* PR/RLR 쓰기 허용 */
  IWDG->PR = 3;                      /* 32분주: LSI/32 카운트 */
  IWDG->RLR = IWDG_RELOAD_VALUE;

  /* PVU/RVU 클리어는 정상이면 LSI 기준 수십~수백us 안에 끝남. 근데 여기가 무한루프면
     워치독을 켜기도 전에(=HUB75_RefreshOnce 루프에 영영 못 들어가서) 화면이 계속
     꺼진 채로 굳어버리는, "멈추면 자동 복구"라는 워치독의 목적과 정반대인 최악의
     상황이 됨 - 그래서 못 끝나도 그냥 넘어가도록 반드시 타임아웃을 둠 */
  for (volatile uint32_t i = 0; (IWDG->SR != 0) && (i < 100000U); i++) { }

  IWDG->KR = 0xAAAA;                 /* 리로드값 반영 */
  IWDG->KR = 0xCCCC;                 /* 워치독 시작 - 이 시점부터 Kick 없으면 리셋됨 */
}

static inline void IWDG_Kick(void)
{
  IWDG->KR = 0xAAAA;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  HUB75_Init();
  HUB75_SetBrightness(30); // 30%로 시작, 눈부시면 더 낮추기
  HUB75_Clear(HUB75_BLACK);
  DrawStaticScene();
  HAL_UART_Receive_IT(&huart2, &rxByte, 1);   // UART 1바이트 수신 대기 시작
  IWDG_Init();   // 이 아래(while문)부터 계속 Kick 안 하면 STM32가 스스로 리셋됨
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    IWDG_Kick();   // 매 반복마다 갱신 - 여기 안 오면(어딘가 멈추면) 몇 초 뒤 자동 리셋

    // 매트릭스는 계속 스캔해줘야 화면이 유지됨 (blocking delay로 막으면 안 됨)
    HUB75_RefreshOnce();

    if (g_inAlertScreen)
    {
      UpdateAlertBorderBlink();
      if (alertShowingEvacuation)
      {
        UpdateEvacPathAnimation();   // 대피경로 점등 애니메이션 (0.5초 주기)
      }
    }
    else
    {
      UpdateColonBlink();   // 시:분 콜론 1초 주기 깜빡임
    }

    PollUartRx();   // 링버퍼에 쌓인 바이트 파싱

    if (g_dataUpdated)   // 새 패킷 도착 -> 화면 갱신 + ACK 응답 (전환 화면 표시 중에는 무시)
    {
      if (!g_inAlertScreen)
      {
        UpdateDynamicDisplay();
        SendAckPacket(0x00);
      }
      g_dataUpdated = 0;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, HUB75_A_Pin|HUB75_B_Pin|HUB75_C_Pin|HUB75_D_Pin
                          |HUB75_E_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LD2_Pin|HUB75_CLK_Pin|HUB75_LAT_Pin|HUB75_OE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, HUB75_B2_Pin|HUB75_G2_Pin|HUB75_R1_Pin|HUB75_G1_Pin
                          |HUB75_B1_Pin|HUB75_R2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : HUB75_A_Pin HUB75_B_Pin HUB75_C_Pin HUB75_D_Pin
                           HUB75_E_Pin */
  GPIO_InitStruct.Pin = HUB75_A_Pin|HUB75_B_Pin|HUB75_C_Pin|HUB75_D_Pin
                          |HUB75_E_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USART2 TX/RX(PA2/PA3) GPIO 설정은 HAL_UART_MspInit()에서 처리 (USART1 때와 동일한 방식) */

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : HUB75_CLK_Pin HUB75_LAT_Pin HUB75_OE_Pin */
  GPIO_InitStruct.Pin = HUB75_CLK_Pin|HUB75_LAT_Pin|HUB75_OE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : HUB75_B2_Pin HUB75_G2_Pin HUB75_R1_Pin HUB75_G1_Pin
                           HUB75_B1_Pin HUB75_R2_Pin */
  GPIO_InitStruct.Pin = HUB75_B2_Pin|HUB75_G2_Pin|HUB75_R1_Pin|HUB75_G1_Pin
                          |HUB75_B1_Pin|HUB75_R2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
