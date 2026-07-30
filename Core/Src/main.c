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
#include "dma.h"
#include "fdcan.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "drv8870.h"
#include "esp32_pid_protocol.h"
#include "line_follower.h"
#include "pid_storage.h"
#include "st7789.h"
#include "task_ui.h"
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
static DRV8870_HandleTypeDef motor1_driver;
static DRV8870_HandleTypeDef motor2_driver;
static uint8_t selected_task;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

typedef void (*App_TaskStartFunction)(void);

static void App_StartTask1(void)
{
  LineFollower_Start();
}

/* Replace these placeholders as the remaining task implementations are added. */
static void App_StartTask2(void) {}
static void App_StartTask3(void) {}
static void App_StartTask4(void) {}
static void App_StartTask5(void) {}
static void App_StartTask6(void)
{
  LineFollower_StartStraight();
}

static const App_TaskStartFunction task_start_functions[TASK_UI_TASK_COUNT] =
{
  App_StartTask1,
  App_StartTask2,
  App_StartTask3,
  App_StartTask4,
  App_StartTask5,
  App_StartTask6
};

static void App_StartSelectedTask(uint8_t task_index)
{
  if (task_index >= TASK_UI_TASK_COUNT)
  {
    return;
  }

  /* Stop the current motion before handing control to another task. */
  LineFollower_Stop();
  task_start_functions[task_index]();
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

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  MX_DMA_Init();
  MX_FDCAN3_Init();
  MX_SPI2_Init();
  MX_SPI5_Init();
  MX_TIM3_Init();
  MX_TIM5_Init();
  MX_TIM15_Init();
  MX_TIM24_Init();
  MX_UART4_Init();
  MX_I2C1_Init();
  MX_UART7_Init();
  MX_UART8_Init();
  MX_USART3_UART_Init();
  MX_TIM17_Init();
  /* USER CODE BEGIN 2 */
  if (DRV8870_Init(&motor1_driver, &htim15,
                   TIM_CHANNEL_1, TIM_CHANNEL_2) != DRV8870_OK)
  {
    Error_Handler();
  }

  if (DRV8870_Init(&motor2_driver, &htim24,
                   TIM_CHANNEL_2, TIM_CHANNEL_1) != DRV8870_OK)
  {
    Error_Handler();
  }

  HAL_GPIO_WritePin(MOS_5V_GPIO_Port, MOS_5V_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(MOS_12V_GPIO_Port, MOS_12V_Pin, GPIO_PIN_SET);
  HAL_Delay(20U);

  if (ST7789_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }

  if (LineFollower_Init(&motor1_driver, &motor2_driver,
                        &htim5, &htim3) != HAL_OK)
  {
    Error_Handler();
  }
  {
    LineFollower_PIDConfigTypeDef pid_config;

    if (LineFollower_GetPIDConfig(&pid_config) != HAL_OK)
    {
      Error_Handler();
    }
    if (PIDStorage_Init(&hi2c1) == PID_STORAGE_OK)
    {
      PIDStorage_StatusTypeDef load_status = PIDStorage_Load(&pid_config);
      if (load_status == PID_STORAGE_OK)
      {
        if (LineFollower_SetPIDConfig(&pid_config) != HAL_OK)
        {
          (void)LineFollower_GetPIDConfig(&pid_config);
          (void)PIDStorage_Save(&pid_config);
        }
      }
      else if (load_status == PID_STORAGE_NOT_FOUND)
      {
        (void)PIDStorage_Save(&pid_config);
      }
    }
  }
  LineFollower_Stop();
  if (ESP32PID_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  if (TaskUI_Init() != HAL_OK)
  {
    Error_Handler();
  }
  __HAL_TIM_SET_COUNTER(&htim17, 0U);
  if (HAL_TIM_Base_Start_IT(&htim17) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    TaskUI_EventTypeDef task_ui_event;

    ESP32PID_Process();
    task_ui_event = TaskUI_Process(&selected_task);
    if (task_ui_event == TASK_UI_EVENT_START)
    {
      App_StartSelectedTask(selected_task);
    }
    else if (task_ui_event == TASK_UI_EVENT_EXIT)
    {
      LineFollower_Stop();
    }

    if ((selected_task == 0U) &&
        (LineFollower_GetState()->stop_marker_tick != 0U))
    {
      TaskUI_StopwatchStopAt(
          LineFollower_GetState()->stop_marker_tick);
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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 44;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

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
