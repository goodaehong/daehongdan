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

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    STATE_WAIT_STX,  // STX(0x02)를 기다리는 상태
    STATE_READ_LEN,  // Length를 읽는 상태
    STATE_READ_CMD,  // Command를 읽는 상태
    STATE_READ_DATA, // Data를 읽는 상태
    STATE_READ_CS,   // Checksum을 읽는 상태
    STATE_WAIT_ETX   // ETX(0x03)를 기다리는 상태
} RX_State_t;

typedef struct {
    uint8_t fan_speed;   // 0x00(OFF), 0x01(약), 0x02(중), 0x03(강)
    uint8_t valve;       // 0x00(닫힘), 0x01(열림)
    uint8_t siren;       // 0x00(OFF), 0x01(ON)
} ActuatorStatus_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PACKET_STX              0x02
#define PACKET_ETX              0x03
#define CMD_FAN_CTRL            0x10
#define CMD_VALVE_CTRL          0x20
#define CMD_SIREN_CTRL          0x30
#define CMD_REQ_STATUS          0x40
#define CMD_GAS_EMERG           0x50
#define CMD_MAX_EMERG           0x60
#define CMD_SYS_RESET           0x70

ActuatorStatus_t current_status = {0x01, 0x01, 0x00};
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
RX_State_t rx_state = STATE_WAIT_STX;
uint8_t rx_byte;             // 방금 수신된 1바이트를 담을 변수
uint8_t rx_len;              // 수신된 Length 값
uint8_t rx_cmd;              // 수신된 Command 값
uint8_t rx_data_buf[10];     // 수신된 Data를 모아둘 배열
uint8_t rx_data_idx = 0;     // Data 배열 인덱스
uint8_t rx_calc_cs = 0;      // 수신하면서 내가 직접 계산(XOR)할 Checksum
uint8_t rx_recv_cs = 0;      // 라즈베리파이가 보낸 Checksum
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
void Send_ACK(uint8_t cmd);
void Send_Status_Response(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_GPIO_TogglePin(GPIOA, LD2_Pin);
    HAL_Delay(1000); 
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
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 83;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 49;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 83;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 999;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

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
  HAL_GPIO_WritePin(GPIOC, RELAY_WARN_Pin|RELAY_VALVE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(PUMP_CTRL_GPIO_Port, PUMP_CTRL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : RESET_BT_Pin */
  GPIO_InitStruct.Pin = RESET_BT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(RESET_BT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : RELAY_WARN_Pin RELAY_VALVE_Pin */
  GPIO_InitStruct.Pin = RELAY_WARN_Pin|RELAY_VALVE_Pin;
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

  /*Configure GPIO pin : FAN_TACH_Pin */
  GPIO_InitStruct.Pin = FAN_TACH_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(FAN_TACH_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PUMP_CTRL_Pin */
  GPIO_InitStruct.Pin = PUMP_CTRL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(PUMP_CTRL_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void Send_ACK(uint8_t cmd) {
    uint8_t tx_buf[6]; // STX(1) + Len(1) + Cmd(1) + CS(1) + ETX(1) = 5바이트면 충분하지만 여유있게 6 설정
    uint8_t checksum = 0;

    tx_buf[0] = PACKET_STX;  // 0x02
    tx_buf[1] = 0;           // Length: 0 (데이터 없음)
    tx_buf[2] = cmd;         // 수신했던 Command ID 그대로 반환

    // 체크섬 계산 (Length부터 Command까지 XOR)
    checksum ^= tx_buf[1];
    checksum ^= tx_buf[2];
    tx_buf[3] = checksum;
    tx_buf[4] = PACKET_ETX;  // 0x03

    // UART2를 통해 5바이트 전송 (타임아웃 100ms)
    // &huart2 부분은 본인의 UART 핸들러 변수명에 맞게 수정하세요.
    HAL_UART_Transmit(&huart2, tx_buf, 5, 100);
}

void Send_Status_Response(void) {
    uint8_t tx_buf[10]; // STX(1) + Len(1) + Cmd(1) + Data(3) + CS(1) + ETX(1) = 총 8바이트
    uint8_t checksum = 0;
    uint8_t idx = 0;

    tx_buf[idx++] = PACKET_STX;        // 0x02
    tx_buf[idx++] = 3;                 // Length: 3 (Data1, Data2, Data3)
    tx_buf[idx++] = CMD_REQ_STATUS;    // 0x40

    // 데이터 영역 채우기 (현재 하드웨어 상태 변수 값 대입)
    tx_buf[idx++] = current_status.fan_speed; // Data1
    tx_buf[idx++] = current_status.valve;     // Data2
    tx_buf[idx++] = current_status.siren;     // Data3

    // 체크섬 계산 (Length부터 모든 Data까지 순서대로 XOR)
    // tx_buf[1]부터 tx_buf[5]까지 계산하게 됩니다.
    for(int i = 1; i < idx; i++) {
        checksum ^= tx_buf[i];
    }
    tx_buf[idx++] = checksum;

    tx_buf[idx++] = PACKET_ETX;        // 0x03

    // 조립된 총 8바이트의 패킷을 라즈베리파이로 전송
    HAL_UART_Transmit(&huart2, tx_buf, idx, 100);
}

void Process_Integrated_Command(uint8_t cmd, uint8_t *data, uint8_t len) {
  switch(cmd) {
    case CMD_FAN_CTRL:
      // 팬 구동 및 속도 제어
      // 0x00(OFF), 0x01(약), 0x02(중), 0x03(강)
      Send_ACK(cmd); // 라즈베리파이로 잘 받았다고 응답
      break;
            
    case CMD_VALVE_CTRL:
      // 솔레노이드 가스 밸브 제어
      //0x00(닫힘), 0x01(열림)
      Send_ACK(cmd);
      break;
          
    case CMD_SIREN_CTRL:
      //사이렌 및 부저 제어
      //0x00(OFF), 0x01(ON)
      Send_ACK(cmd);
      break;

    case CMD_REQ_STATUS:
      //액추에이터 현재 상태 요청
      Send_Status_Response(); // 상태 데이터 4바이트를 담아서 응답
      break;
    
    case CMD_GAS_EMERG:
      //가스 누출 위험 대응
      //사이렌 ON + 밸브 즉시 차단 + 환기팬(강)
      Send_ACK(cmd);
      break;

    case CMD_MAX_EMERG:
      //최고 수준 비상 대응
      //사이렌 ON + 밸브 차단 + 환기팬 OFF
      Send_ACK(cmd);
      break;

    case CMD_SYS_RESET:
      //비상 상황 해제
      //사이렌 OFF + 밸브 오픈 + 환기팬(약)
      Send_ACK(cmd);
      break;
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART2) {
    switch (rx_state) {
      case STATE_WAIT_STX:
        if (rx_byte == PACKET_STX) {  // 0x02가 들어오면 패킷 시작
          rx_state = STATE_READ_LEN;
          rx_calc_cs = 0;           // 체크섬 계산 초기화
        }
        break;

      case STATE_READ_LEN:
        rx_len = rx_byte;
        rx_calc_cs ^= rx_byte;        // XOR 체크섬 계산 누적
        rx_data_idx = 0;
        rx_state = STATE_READ_CMD;
        break;

      case STATE_READ_CMD:
        rx_cmd = rx_byte;
        rx_calc_cs ^= rx_byte;
                
        // 데이터가 있으면 DATA 상태로, 없으면(Length==0) 바로 CS 상태로
        if (rx_len > 0) {
          rx_state = STATE_READ_DATA;
        } else {
          rx_state = STATE_READ_CS;
        }
        break;

      case STATE_READ_DATA:
        rx_data_buf[rx_data_idx++] = rx_byte;
        rx_calc_cs ^= rx_byte;
                
        // 약속한 길이만큼 다 받았으면
        if (rx_data_idx >= rx_len) {
          rx_state = STATE_READ_CS;
        }
        break;

      case STATE_READ_CS:
        rx_recv_cs = rx_byte;
        rx_state = STATE_WAIT_ETX;
        break;

      case STATE_WAIT_ETX:
        if (rx_byte == PACKET_ETX) {  // 0x03이 들어오면 패킷 종료
          // 통신 중 데이터가 깨지지 않았는지 검증
          if (rx_calc_cs == rx_recv_cs) {
            // 완벽한 패킷 수신 성공! 명령어 처리 함수 호출
            Process_Integrated_Command(rx_cmd, rx_data_buf, rx_len);
          }
        }
        // 패킷 처리가 끝났거나 실패했으므로 다시 처음 상태로 초기화
        rx_state = STATE_WAIT_STX;
        break;
    }
  // 다음 1바이트 수신을 위해 인터럽트 다시 장전 (필수!)
  HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
  }
}

// 외부 인터럽트(핀 상태 변화)가 발생하면 자동으로 호출되는 콜백 함수
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  // 방금 눌린 핀이 파란색 버튼(PC13)인지 확인
  if(GPIO_Pin == GPIO_PIN_13)
  {
      // --------------------------------------------------
      // 여기에 레귤레이터 초기화 코드를 작성합니다.
      // --------------------------------------------------
      
      // 예시 1: 펌프 릴레이(PB0) 강제 정지
      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
      
      // 예시 2: 밸브(PC1) 강제 차단
      HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_RESET);
      
      // 예시 3: 부저(PB6) 끄기 (타이머 PWM 정지)
      // HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_1);
      
      // 상태 초기화 플래그 등...
  }
}
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
