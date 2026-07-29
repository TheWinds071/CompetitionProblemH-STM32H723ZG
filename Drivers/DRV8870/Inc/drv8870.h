#ifndef DRV8870_H
#define DRV8870_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#include <stdint.h>

#define DRV8870_SPEED_MAX 1000

typedef enum
{
  DRV8870_OK = 0,
  DRV8870_ERROR_PARAMETER,
  DRV8870_ERROR_NOT_INITIALIZED,
  DRV8870_ERROR_HAL
} DRV8870_StatusTypeDef;

typedef enum
{
  DRV8870_MODE_COAST = 0,
  DRV8870_MODE_BRAKE,
  DRV8870_MODE_FORWARD,
  DRV8870_MODE_REVERSE
} DRV8870_ModeTypeDef;

typedef struct
{
  TIM_HandleTypeDef *timer;
  uint32_t in1_channel;
  uint32_t in2_channel;
  uint32_t pwm_period;
  int16_t speed;
  DRV8870_ModeTypeDef mode;
  uint8_t initialized;
} DRV8870_HandleTypeDef;

DRV8870_StatusTypeDef DRV8870_Init(DRV8870_HandleTypeDef *driver,
                                    TIM_HandleTypeDef *timer,
                                    uint32_t in1_channel,
                                    uint32_t in2_channel);
DRV8870_StatusTypeDef DRV8870_SetSpeed(DRV8870_HandleTypeDef *driver,
                                        int16_t speed);
DRV8870_StatusTypeDef DRV8870_Brake(DRV8870_HandleTypeDef *driver);
DRV8870_StatusTypeDef DRV8870_Coast(DRV8870_HandleTypeDef *driver);
DRV8870_StatusTypeDef DRV8870_DeInit(DRV8870_HandleTypeDef *driver);

#ifdef __cplusplus
}
#endif

#endif
