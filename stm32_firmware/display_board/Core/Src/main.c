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
// 표정 3종 3초마다 순환 테스트: 웃음(초록) -> 무표정(노랑) -> 찡그림(빨강)
static const uint32_t * const faceShapes[3] = { HUB75_Shape11, HUB75_Shape8, HUB75_Shape12 };
static const HUB75_Color faceColors[3] = { HUB75_GREEN, HUB75_YELLOW, HUB75_RED };

static void DrawFace(uint8_t index)
{
  HUB75_FillRect(38, 2, 32, 21, HUB75_BLACK);   // 이전 표정 지우기
  DrawBitmap32(38, 2, faceShapes[index], 21, faceColors[index]);
  DrawBitmap16(26, 8, HUB75_Korean2, 10, HUB75_WHITE);   // "역" 글자가 지우는 영역과 겹쳐서 같이 다시 그림
}

// 가스 그래프 채워지는 높이: 웃음=2줄(초록), 무표정=5줄(노랑), 찡그림=10줄(빨강) - 아래부터 채워짐
static const uint8_t gasFillRows[3] = { 2, 5, 11 };
static const HUB75_Color gasColors[3] = { HUB75_GREEN, HUB75_YELLOW, HUB75_RED };

static void DrawGasGraph(uint8_t index)
{
  HUB75_FillRect(22, 40, 16, 11, HUB75_BLACK);   // 이전 채움 지우기

  uint8_t filledRows = gasFillRows[index];
  uint8_t whiteRows = 11 - filledRows;

  if (whiteRows > 0)
  {
    DrawBitmap16(22, 40, HUB75_Shape7, whiteRows, HUB75_WHITE);           // 위쪽 나머지 줄은 흰색
  }
  DrawBitmap16(22, 40 + whiteRows, HUB75_Shape7, filledRows, gasColors[index]); // 아래쪽 채워진 줄

  // 지우는 영역과 겹치는 요소들 다시 그림
  DrawBitmap16(11, 41, HUB75_Korean4, 9, HUB75_CYAN);   // 스
  DrawBitmap8(29, 41, HUB75_Number1, 8, HUB75_CYAN);    // 가스 농도 천의 자리
  DrawBitmap8(34, 41, HUB75_Number6, 8, HUB75_CYAN);    // 가스 농도 백의 자리
}

// 좌표는 (1,1)~(64,64) 기준표를 0-index로 변환(-1)해서 배치. 지금은 랜덤 없이 표에 있는 값 그대로 고정 표시.
static void DrawStaticScene(void)
{
  DrawBitmap8(6, 8, HUB75_Alphabet, 9, HUB75_WHITE);          // A
  DrawBitmap16(16, 8, HUB75_Korean1, 10, HUB75_WHITE);        // 구
  DrawBitmap16(26, 8, HUB75_Korean2, 10, HUB75_WHITE);        // 역
  DrawBitmap16(2, 41, HUB75_Korean3, 9, HUB75_CYAN);          // 가
  DrawBitmap16(11, 41, HUB75_Korean4, 9, HUB75_CYAN);         // 스
  DrawBitmap16(1, 24, HUB75_Shape, 13, HUB75_YELLOW);         // 온도(태양)
  DrawBitmap8(55, 27, HUB75_Shape2, 8, HUB75_BLUE);           // %
  DrawBitmap8(25, 27, HUB75_Shape3, 8, HUB75_YELLOW);         // 온도(섭씨)
  DrawBitmap8(49, 42, HUB75_Shape4, 8, HUB75_CYAN);           // 가스 농도(pp)
  DrawBitmap8(57, 42, HUB75_Shape5, 8, HUB75_CYAN);           // 가스 농도(m)
  DrawBitmap16(35, 25, HUB75_Shape6, 10, HUB75_BLUE);         // 습도(물방울)
  DrawFace(0);                                                // 표정 (첫 프레임: 웃음)
  DrawGasGraph(0);                                            // 가스 그래프 (첫 프레임: 웃음 상태)
  DrawBitmap8(11, 53, HUB75_Shape9, 8, HUB75_WHITE);          // .
  DrawBitmap8(23, 53, HUB75_Shape9, 8, HUB75_WHITE);          // .
  DrawBitmap8(51, 54, HUB75_Shape10, 8, HUB75_WHITE);         // :

  DrawBitmap8(16, 27, HUB75_Number3, 8, HUB75_YELLOW);        // 온도 십의 자리
  DrawBitmap8(21, 27, HUB75_Number1, 8, HUB75_YELLOW);        // 온도 일의 자리
  DrawBitmap8(44, 27, HUB75_Number3, 8, HUB75_BLUE);          // 습도 십의 자리
  DrawBitmap8(49, 27, HUB75_Number5, 8, HUB75_BLUE);          // 습도 일의 자리

  DrawBitmap8(29, 41, HUB75_Number1, 8, HUB75_CYAN);          // 가스 농도 천의 자리
  DrawBitmap8(34, 41, HUB75_Number6, 8, HUB75_CYAN);          // 가스 농도 백의 자리
  DrawBitmap8(39, 41, HUB75_Number0, 8, HUB75_CYAN);          // 가스 농도 십의 자리
  DrawBitmap8(44, 41, HUB75_Number0, 8, HUB75_CYAN);          // 가스 농도 일의 자리

  DrawBitmap8(2, 53, HUB75_TinyNumber2, 8, HUB75_WHITE);      // 연도 십의 자리
  DrawBitmap8(7, 53, HUB75_TinyNumber6, 8, HUB75_WHITE);      // 연도 일의 자리
  DrawBitmap8(14, 53, HUB75_TinyNumber0, 8, HUB75_WHITE);     // 월 십의 자리
  DrawBitmap8(19, 53, HUB75_TinyNumber7, 8, HUB75_WHITE);     // 월 일의 자리
  DrawBitmap8(26, 53, HUB75_TinyNumber1, 8, HUB75_WHITE);     // 일 십의 자리
  DrawBitmap8(31, 53, HUB75_TinyNumber5, 8, HUB75_WHITE);     // 일 일의 자리
  DrawBitmap8(41, 53, HUB75_TinyNumber1, 8, HUB75_WHITE);     // 시 십의 자리
  DrawBitmap8(46, 53, HUB75_TinyNumber0, 8, HUB75_WHITE);     // 시 일의 자리
  DrawBitmap8(53, 53, HUB75_TinyNumber3, 8, HUB75_WHITE);     // 분 십의 자리
  DrawBitmap8(58, 53, HUB75_TinyNumber7, 8, HUB75_WHITE);     // 분 일의 자리
}

/* ===================== Pi <-> STM32 UART 프로토콜 ===================== */
/* STX/ETX는 실제 팀 프로토콜 값으로 확정되면 여기만 바꾸면 됨 */
#define PACKET_STX     0xAA
#define PACKET_ETX     0x55
#define CMD_UPDATE     0x80   /* 평상시 화면 갱신 (Pi -> STM32) */
#define CMD_ACK        0xB0   /* 전광판 상태 응답 (STM32 -> Pi) */
#define UPDATE_DATA_LEN 11    /* 표정,가스색,가스H,가스L,온도,습도,시,분,년,월,일 */

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
static uint8_t packetLen, packetCmd, packetData[16], packetDataIdx;

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
    g_face      = data[0];
    g_gasColor  = data[1];
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
}

/* 링버퍼에서 바이트 하나씩 꺼내 상태기계로 패킷 조립 */
static void ProcessRxByte(uint8_t b)
{
  static uint8_t checksumCalc;

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
}

static void UpdateDigit(int16_t x, int16_t y, uint8_t digit, HUB75_Color color, uint8_t tiny)
{
  HUB75_FillRect(x, y, 8, 8, HUB75_BLACK);
  DrawBitmap8(x, y, tiny ? TinyNumberDigits[digit] : NumberDigits[digit], 8, color);
}

/* 수신값을 실제 화면에 반영 */
static void UpdateDynamicDisplay(void)
{
  DrawFace(g_face);
  DrawGasGraph(g_gasColor);

  /* 가스 농도 4자리 (천/백/십/일) */
  UpdateDigit(29, 41, (uint8_t)((g_gas / 1000) % 10), HUB75_CYAN, 0);
  UpdateDigit(34, 41, (uint8_t)((g_gas / 100) % 10), HUB75_CYAN, 0);
  UpdateDigit(39, 41, (uint8_t)((g_gas / 10) % 10), HUB75_CYAN, 0);
  UpdateDigit(44, 41, (uint8_t)(g_gas % 10), HUB75_CYAN, 0);

  /* 온도 */
  UpdateDigit(16, 27, (uint8_t)(g_temp / 10), HUB75_YELLOW, 0);
  UpdateDigit(21, 27, (uint8_t)(g_temp % 10), HUB75_YELLOW, 0);

  /* 습도 */
  UpdateDigit(44, 27, (uint8_t)(g_humidity / 10), HUB75_BLUE, 0);
  UpdateDigit(49, 27, (uint8_t)(g_humidity % 10), HUB75_BLUE, 0);

  /* 시:분 */
  UpdateDigit(41, 53, (uint8_t)(g_hour / 10), HUB75_WHITE, 1);
  UpdateDigit(46, 53, (uint8_t)(g_hour % 10), HUB75_WHITE, 1);
  UpdateDigit(53, 53, (uint8_t)(g_minute / 10), HUB75_WHITE, 1);
  UpdateDigit(58, 53, (uint8_t)(g_minute % 10), HUB75_WHITE, 1);

  /* 년/월/일 */
  UpdateDigit(2, 53, (uint8_t)(g_year / 10), HUB75_WHITE, 1);
  UpdateDigit(7, 53, (uint8_t)(g_year % 10), HUB75_WHITE, 1);
  UpdateDigit(14, 53, (uint8_t)(g_month / 10), HUB75_WHITE, 1);
  UpdateDigit(19, 53, (uint8_t)(g_month % 10), HUB75_WHITE, 1);
  UpdateDigit(26, 53, (uint8_t)(g_day / 10), HUB75_WHITE, 1);
  UpdateDigit(31, 53, (uint8_t)(g_day % 10), HUB75_WHITE, 1);
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
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t lastHeartbeat = HAL_GetTick();

  while (1)
  {
    // 매트릭스는 계속 스캔해줘야 화면이 유지됨 (blocking delay로 막으면 안 됨)
    HUB75_RefreshOnce();

    uint32_t now = HAL_GetTick();
    if (now - lastHeartbeat >= 500)
    {
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);   // 보드 내장 LED 하트비트
      lastHeartbeat = now;
    }

    PollUartRx();   // 링버퍼에 쌓인 바이트 파싱

    if (g_dataUpdated)   // 새 패킷 도착 -> 화면 갱신 + ACK 응답
    {
      UpdateDynamicDisplay();
      SendAckPacket(0x00);
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
  HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
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
