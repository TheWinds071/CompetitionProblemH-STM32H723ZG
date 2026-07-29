#ifndef LINE_FOLLOWER_H
#define LINE_FOLLOWER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "drv8870.h"
#include "stm32h7xx_hal.h"

#include <stdint.h>

/* All speed values are encoder counts per 1 ms control period. */
#define LINE_FOLLOW_CONTROL_PERIOD_MS       1U
#define LINE_FOLLOW_BASE_SPEED_TICKS        2.5F
#define LINE_FOLLOW_MAX_SPEED_TICKS         4.5F
#define LINE_FOLLOW_MAX_STEERING_TICKS      2.0F
#define LINE_FOLLOW_LOST_SEARCH_TICKS       1.2F
#define LINE_FOLLOW_LOST_STOP_CYCLES        500U

#define LINE_FOLLOW_STEERING_KP             0.00070F
#define LINE_FOLLOW_STEERING_KI             0.0000F
#define LINE_FOLLOW_STEERING_KD             0.000015F

#define LINE_FOLLOW_SPEED_KP                220.0F
#define LINE_FOLLOW_SPEED_KI                800.0F
#define LINE_FOLLOW_SPEED_KD                0.0F

/* Change either sign to -1 if its forward encoder count is negative. */
#define LINE_FOLLOW_LEFT_ENCODER_SIGN       1
#define LINE_FOLLOW_RIGHT_ENCODER_SIGN      1

typedef struct
{
  uint8_t gray_raw_mask;
  uint8_t gray_active_mask;
  uint8_t gray_active_count;
  uint8_t line_detected;
  uint16_t line_lost_cycles;
  int16_t line_position;
  int32_t left_encoder_delta;
  int32_t right_encoder_delta;
  float left_target_speed;
  float right_target_speed;
  int16_t left_pwm;
  int16_t right_pwm;
} LineFollower_StateTypeDef;

HAL_StatusTypeDef LineFollower_Init(DRV8870_HandleTypeDef *left_motor,
                                    DRV8870_HandleTypeDef *right_motor,
                                    TIM_HandleTypeDef *left_encoder,
                                    TIM_HandleTypeDef *right_encoder);
void LineFollower_Start(void);
void LineFollower_Stop(void);
void LineFollower_Update(void);
const LineFollower_StateTypeDef *LineFollower_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* LINE_FOLLOWER_H */