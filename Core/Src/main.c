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
#include "dma.h"
#include "gpio.h"
#include "main.h"
#include "tim.h"
#include "usart.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "BootLoader.h"
#include "BootLoader_common.h"
#include "ModbusRTU.h"
#include "XMODEM.h"
#include "fsm_bootloader.h"
#include "fsm_event.h"
#include "ulog.h"

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

/* USER CODE BEGIN PV */
Modbus_CommContext mb_comm = {0};
volatile uint8_t rxdata;

uint16_t Modbus_holding_regs[MODBUS_REG_COUNT] = {0};
volatile uint16_t Modbus_read_regs[MODBUS_REG_COUNT] = {0};
volatile uint16_t Modbus_write_regs[MODBUS_REG_COUNT] = {0};

MODBUS_Device Modbus_dev0;
XMODEM_Device xmodem;

Fsm_Struct state_descriptor[] = {
    [STATE_MODBUS_RECV] = {.state = STATE_MODBUS_RECV,
                           .entry = ModbusRecv_Entry,
                           .do_action = ModbusRecv_Do,
                           .exit = ModbusRecv_Exit},
    [STATE_PROG_UPGRADE] = {.state = STATE_PROG_UPGRADE,
                            .entry = ProgUpgrade_Entry,
                            .do_action = ProgUpgrade_Do,
                            .exit = ProgUpgrade_Exit},
    [STATE_JUMP_APP] = {.state = STATE_JUMP_APP,
                        .entry = JumpApp_Entry,
                        .do_action = JumpApp_Do,
                        .exit = JumpApp_Exit}};

version_info_t version;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void LED_MODE(uint8_t state) {
  static uint32_t lastticks = 0;
  static uint32_t gapticks = 0;
  if (HAL_GetTick() - lastticks > gapticks) {
    lastticks = HAL_GetTick();
    HAL_GPIO_TogglePin(LD0_GPIO_Port, LD0_Pin);
  }

  switch (state) {
  case 0:
    gapticks = 50; // Set gap time for state 0
    break;
  case 1:
    gapticks = 100; // Set gap time for state 1
    break;
  default:
    gapticks = 500; // Set gap time for default state
    break;
  }
}

void my_console_logger(ulog_level_t lvl, const char *msg) {
  printf("[%lu][%s]: %s\n", HAL_GetTick(), ulog_level_name(lvl), msg);
}
/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick.
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_TIM4_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_UARTEx_ReceiveToIdle_IT(&huart1, mb_comm.rx_buffer,
                               MODBUS_RX_BUFFER_SIZE);
  MODBUS_Init(&Modbus_dev0, 19, Modbus_holding_regs, Modbus_read_regs,
              Modbus_write_regs, MODBUS_REG_COUNT);
  Fsm_Init();
  XMODEM_Init(&xmodem, &huart1, Bootloader_FlashWriteBuffer, 2000, 5);
  Bootloader_Init();
  ULOG_INIT();
  ULOG_SUBSCRIBE(my_console_logger, ULOG_DEBUG_LEVEL);
  HAL_TIM_Base_Start_IT(&htim4);
  ULOG_INFO("Boot version %d:%d:%d\r\n", BOOT_VER_MAJOR, BOOT_VER_MINOR,
            BOOT_VER_PATCH);
  Version_Read(&version);
  ULOG_INFO("APP version %d:%d:%d\n", version.app_ver_major,
            version.app_ver_minor, version.app_ver_patch);
  ULOG_INFO("App size is %d bytes\n", version.app_end_addr - APP_ADDRESS);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1) {
    Fsm_Process();
  }

  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
   * in the RCC_OscInitTypeDef structure.
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart->Instance == USART1) {
		if(Fsm_IsInState() == STATE_PROG_UPGRADE)
		{
			XMODEM_UART_RxEventCallback(&xmodem, Size);
		}else{
			mb_comm.rx_len = Size;
			HAL_UARTEx_ReceiveToIdle_IT(&huart1, mb_comm.rx_buffer,
                               MODBUS_RX_BUFFER_SIZE);
		}
    
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {

if (huart->Instance == USART1) {

}
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == TIM4) {
    Fsm_Run();
    LED_MODE(Fsm_IsInState());
  }
}
/* USER CODE END 4 */

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
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
void assert_failed(uint8_t *file, uint32_t line) {
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
