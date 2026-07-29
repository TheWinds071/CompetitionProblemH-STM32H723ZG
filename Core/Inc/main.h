/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define MOS_12V_Pin GPIO_PIN_3
#define MOS_12V_GPIO_Port GPIOF
#define Buzzer_Pin GPIO_PIN_5
#define Buzzer_GPIO_Port GPIOF
#define SPI5_CS_Pin GPIO_PIN_6
#define SPI5_CS_GPIO_Port GPIOF
#define ENCODER1_1_Pin GPIO_PIN_0
#define ENCODER1_1_GPIO_Port GPIOA
#define ENCODER1_2_Pin GPIO_PIN_1
#define ENCODER1_2_GPIO_Port GPIOA
#define MOTOR1_1_Pin GPIO_PIN_2
#define MOTOR1_1_GPIO_Port GPIOA
#define MOTOR1_2_Pin GPIO_PIN_3
#define MOTOR1_2_GPIO_Port GPIOA
#define ENCODER2_1_Pin GPIO_PIN_6
#define ENCODER2_1_GPIO_Port GPIOA
#define ENCODER2_2_Pin GPIO_PIN_7
#define ENCODER2_2_GPIO_Port GPIOA
#define MOTOR2_1_Pin GPIO_PIN_11
#define MOTOR2_1_GPIO_Port GPIOF
#define MOTOR2_2_Pin GPIO_PIN_12
#define MOTOR2_2_GPIO_Port GPIOF
#define LCD_SCK_Pin GPIO_PIN_13
#define LCD_SCK_GPIO_Port GPIOB
#define LCD_D_C_Pin GPIO_PIN_14
#define LCD_D_C_GPIO_Port GPIOB
#define LCD_MOSI_Pin GPIO_PIN_15
#define LCD_MOSI_GPIO_Port GPIOB
#define LCD_RESET_Pin GPIO_PIN_8
#define LCD_RESET_GPIO_Port GPIOD
#define LCD_CS_Pin GPIO_PIN_9
#define LCD_CS_GPIO_Port GPIOD
#define Button1_Pin GPIO_PIN_10
#define Button1_GPIO_Port GPIOD
#define Button2_Pin GPIO_PIN_11
#define Button2_GPIO_Port GPIOD
#define Button3_Pin GPIO_PIN_14
#define Button3_GPIO_Port GPIOD
#define MOS_5V_Pin GPIO_PIN_10
#define MOS_5V_GPIO_Port GPIOA
#define ESP32_TX_Pin GPIO_PIN_10
#define ESP32_TX_GPIO_Port GPIOC
#define ESP32_RX_Pin GPIO_PIN_11
#define ESP32_RX_GPIO_Port GPIOC
#define LED_G_Pin GPIO_PIN_11
#define LED_G_GPIO_Port GPIOG
#define LED_R_Pin GPIO_PIN_12
#define LED_R_GPIO_Port GPIOG
#define LED_B_Pin GPIO_PIN_13
#define LED_B_GPIO_Port GPIOG
#define L3_Pin GPIO_PIN_14
#define L3_GPIO_Port GPIOG
#define L2_Pin GPIO_PIN_15
#define L2_GPIO_Port GPIOG
#define L1_Pin GPIO_PIN_3
#define L1_GPIO_Port GPIOB
#define M_Pin GPIO_PIN_4
#define M_GPIO_Port GPIOB
#define R1_Pin GPIO_PIN_5
#define R1_GPIO_Port GPIOB
#define R2_Pin GPIO_PIN_6
#define R2_GPIO_Port GPIOB
#define R3_Pin GPIO_PIN_7
#define R3_GPIO_Port GPIOB
#define EEPROM_SCL_Pin GPIO_PIN_8
#define EEPROM_SCL_GPIO_Port GPIOB
#define EEPROM_SDA_Pin GPIO_PIN_9
#define EEPROM_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
