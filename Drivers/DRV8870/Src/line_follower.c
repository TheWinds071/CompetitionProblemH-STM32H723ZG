#include "line_follower.h"

#include "main.h"
#include "pid.h"

#include <stddef.h>

#define GRAY_SENSOR_COUNT 7U
#define GRAY_POSITION_LOST 4000

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  int16_t weight;
} GraySensor_TypeDef;

/* main.h order: L3, L2, L1, M, R1, R2, R3. Inputs are active low. */
static const GraySensor_TypeDef gray_sensors[GRAY_SENSOR_COUNT] =
{
  {L3_GPIO_Port, L3_Pin, -3000},
  {L2_GPIO_Port, L2_Pin, -2000},
  {L1_GPIO_Port, L1_Pin, -1000},
  {M_GPIO_Port,  M_Pin,      0},
  {R1_GPIO_Port, R1_Pin,  1000},
  {R2_GPIO_Port, R2_Pin,  2000},
  {R3_GPIO_Port, R3_Pin,  3000}
};

static DRV8870_HandleTypeDef *left_motor_handle;
static DRV8870_HandleTypeDef *right_motor_handle;
static TIM_HandleTypeDef *left_encoder_handle;
static TIM_HandleTypeDef *right_encoder_handle;
static uint32_t left_encoder_previous;
static uint32_t right_encoder_previous;
static PID_HandleTypeDef steering_pid;
static PID_HandleTypeDef left_speed_pid;
static PID_HandleTypeDef right_speed_pid;
static LineFollower_StateTypeDef control_state;
static int16_t last_line_position;
static uint8_t control_initialized;
static uint8_t control_enabled;

static float LineFollower_ClampFloat(float value, float minimum, float maximum)
{
  if (value > maximum)
  {
    return maximum;
  }
  if (value < minimum)
  {
    return minimum;
  }
  return value;
}

static int16_t LineFollower_RoundToInt16(float value)
{
  if (value >= 0.0F)
  {
    return (int16_t)(value + 0.5F);
  }
  return (int16_t)(value - 0.5F);
}

static int32_t LineFollower_ReadEncoderDelta(TIM_HandleTypeDef *encoder,
                                             uint32_t *previous)
{
  uint32_t current = __HAL_TIM_GET_COUNTER(encoder);
  int32_t delta;

  if (__HAL_TIM_GET_AUTORELOAD(encoder) <= UINT16_MAX)
  {
    delta = (int32_t)(int16_t)((uint16_t)current - (uint16_t)(*previous));
  }
  else
  {
    delta = (int32_t)(current - *previous);
  }

  *previous = current;
  return delta;
}

static uint8_t LineFollower_ReadGray(int16_t *position)
{
  int32_t weighted_sum = 0;
  uint8_t raw_mask = 0U;
  uint8_t active_mask = 0U;
  uint8_t active_count = 0U;
  uint8_t index;

  for (index = 0U; index < GRAY_SENSOR_COUNT; ++index)
  {
    GPIO_PinState pin_state =
        HAL_GPIO_ReadPin(gray_sensors[index].port, gray_sensors[index].pin);

    if (pin_state == GPIO_PIN_SET)
    {
      raw_mask |= (uint8_t)(1U << index);
    }
    else
    {
      active_mask |= (uint8_t)(1U << index);
      weighted_sum += gray_sensors[index].weight;
      ++active_count;
    }
  }

  control_state.gray_raw_mask = raw_mask;
  control_state.gray_active_mask = active_mask;
  control_state.gray_active_count = active_count;

  if (active_count == 0U)
  {
    return 0U;
  }

  *position = (int16_t)(weighted_sum / active_count);
  return 1U;
}

static void LineFollower_ResetControllers(void)
{
  PID_Reset(&steering_pid);
  PID_Reset(&left_speed_pid);
  PID_Reset(&right_speed_pid);
}

HAL_StatusTypeDef LineFollower_Init(DRV8870_HandleTypeDef *left_motor,
                                    DRV8870_HandleTypeDef *right_motor,
                                    TIM_HandleTypeDef *left_encoder,
                                    TIM_HandleTypeDef *right_encoder)
{
  if ((left_motor == NULL) || (right_motor == NULL) ||
      (left_encoder == NULL) || (right_encoder == NULL))
  {
    return HAL_ERROR;
  }

  left_motor_handle = left_motor;
  right_motor_handle = right_motor;
  left_encoder_handle = left_encoder;
  right_encoder_handle = right_encoder;

  if ((HAL_TIM_Encoder_Start(left_encoder_handle, TIM_CHANNEL_ALL) != HAL_OK) ||
      (HAL_TIM_Encoder_Start(right_encoder_handle, TIM_CHANNEL_ALL) != HAL_OK))
  {
    return HAL_ERROR;
  }

  __HAL_TIM_SET_COUNTER(left_encoder_handle, 0U);
  __HAL_TIM_SET_COUNTER(right_encoder_handle, 0U);
  left_encoder_previous = 0U;
  right_encoder_previous = 0U;

  PID_Init(&steering_pid,
           LINE_FOLLOW_STEERING_KP,
           LINE_FOLLOW_STEERING_KI,
           LINE_FOLLOW_STEERING_KD,
           -LINE_FOLLOW_MAX_STEERING_TICKS,
           LINE_FOLLOW_MAX_STEERING_TICKS,
           -LINE_FOLLOW_MAX_STEERING_TICKS,
           LINE_FOLLOW_MAX_STEERING_TICKS,
           0.02F);
  PID_Init(&left_speed_pid,
           LINE_FOLLOW_SPEED_KP,
           LINE_FOLLOW_SPEED_KI,
           LINE_FOLLOW_SPEED_KD,
           -(float)DRV8870_SPEED_MAX,
           (float)DRV8870_SPEED_MAX,
           -400.0F,
           400.0F,
           0.01F);
  PID_Init(&right_speed_pid,
           LINE_FOLLOW_SPEED_KP,
           LINE_FOLLOW_SPEED_KI,
           LINE_FOLLOW_SPEED_KD,
           -(float)DRV8870_SPEED_MAX,
           (float)DRV8870_SPEED_MAX,
           -400.0F,
           400.0F,
           0.01F);

  control_state = (LineFollower_StateTypeDef){0};
  last_line_position = 0;
  control_enabled = 0U;
  control_initialized = 1U;
  (void)DRV8870_Brake(left_motor_handle);
  (void)DRV8870_Brake(right_motor_handle);
  return HAL_OK;
}

void LineFollower_Start(void)
{
  if (control_initialized == 0U)
  {
    return;
  }

  left_encoder_previous = __HAL_TIM_GET_COUNTER(left_encoder_handle);
  right_encoder_previous = __HAL_TIM_GET_COUNTER(right_encoder_handle);
  control_state.line_lost_cycles = 0U;
  LineFollower_ResetControllers();
  control_enabled = 1U;
}

void LineFollower_Stop(void)
{
  control_enabled = 0U;
  LineFollower_ResetControllers();
  control_state.left_target_speed = 0.0F;
  control_state.right_target_speed = 0.0F;
  control_state.left_pwm = 0;
  control_state.right_pwm = 0;

  if (control_initialized != 0U)
  {
    (void)DRV8870_Brake(left_motor_handle);
    (void)DRV8870_Brake(right_motor_handle);
  }
}

void LineFollower_Update(void)
{
  const float dt_seconds = (float)LINE_FOLLOW_CONTROL_PERIOD_MS / 1000.0F;
  float steering_correction;
  float left_target;
  float right_target;
  float left_output;
  float right_output;
  int16_t line_position = last_line_position;

  if (control_initialized == 0U)
  {
    return;
  }

  control_state.left_encoder_delta =
      LineFollower_ReadEncoderDelta(left_encoder_handle,
                                    &left_encoder_previous) *
      LINE_FOLLOW_LEFT_ENCODER_SIGN;
  control_state.right_encoder_delta =
      LineFollower_ReadEncoderDelta(right_encoder_handle,
                                    &right_encoder_previous) *
      LINE_FOLLOW_RIGHT_ENCODER_SIGN;

  control_state.line_detected = LineFollower_ReadGray(&line_position);
  if (control_state.line_detected != 0U)
  {
    control_state.line_lost_cycles = 0U;
    last_line_position = line_position;
  }
  else
  {
    if (control_state.line_lost_cycles < UINT16_MAX)
    {
      ++control_state.line_lost_cycles;
    }
    line_position = (last_line_position >= 0) ?
                    GRAY_POSITION_LOST : -GRAY_POSITION_LOST;
  }
  control_state.line_position = line_position;

  if (control_enabled == 0U)
  {
    return;
  }

  if (control_state.line_lost_cycles >= LINE_FOLLOW_LOST_STOP_CYCLES)
  {
    LineFollower_Stop();
    return;
  }

  steering_correction = PID_Update(&steering_pid,
                                   (float)line_position,
                                   dt_seconds);

  if (control_state.line_detected != 0U)
  {
    left_target = LINE_FOLLOW_BASE_SPEED_TICKS + steering_correction;
    right_target = LINE_FOLLOW_BASE_SPEED_TICKS - steering_correction;
  }
  else
  {
    float search_direction = (line_position >= 0) ? 1.0F : -1.0F;
    left_target = LINE_FOLLOW_LOST_SEARCH_TICKS * search_direction;
    right_target = -LINE_FOLLOW_LOST_SEARCH_TICKS * search_direction;
  }

  left_target = LineFollower_ClampFloat(left_target,
                                        -LINE_FOLLOW_MAX_SPEED_TICKS,
                                        LINE_FOLLOW_MAX_SPEED_TICKS);
  right_target = LineFollower_ClampFloat(right_target,
                                         -LINE_FOLLOW_MAX_SPEED_TICKS,
                                         LINE_FOLLOW_MAX_SPEED_TICKS);
  control_state.left_target_speed = left_target;
  control_state.right_target_speed = right_target;

  left_output = PID_Update(&left_speed_pid,
                           left_target -
                           (float)control_state.left_encoder_delta,
                           dt_seconds);
  right_output = PID_Update(&right_speed_pid,
                            right_target -
                            (float)control_state.right_encoder_delta,
                            dt_seconds);
  control_state.left_pwm = LineFollower_RoundToInt16(left_output);
  control_state.right_pwm = LineFollower_RoundToInt16(right_output);

  (void)DRV8870_SetSpeed(left_motor_handle, control_state.left_pwm);
  (void)DRV8870_SetSpeed(right_motor_handle, control_state.right_pwm);
}

const LineFollower_StateTypeDef *LineFollower_GetState(void)
{
  return &control_state;
}