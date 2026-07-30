#include "line_follower.h"

#include "main.h"
#include "pid.h"

#include <math.h>
#include <stddef.h>

#define GRAY_SENSOR_COUNT 4U
#define GRAY_L2_ACTIVE_MASK (1U << 1U)
#define GRAY_L1_ACTIVE_MASK (1U << 2U)
#define GRAY_STOP_LEFT_MASK  0x07U
#define GRAY_STOP_RIGHT_MASK 0x0EU
#define GRAY_STOP_ALL_MASK   0x0FU

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  int16_t weight;
} GraySensor_TypeDef;

typedef enum
{
  LINE_FOLLOWER_MODE_FOLLOW = 0U,
  LINE_FOLLOWER_MODE_STRAIGHT
} LineFollower_ModeTypeDef;

/* Physical order from left to right: L3, L2, L1, M.
 * The center lies between L2 and L1.
 * Black line is GPIO low (active); white background is GPIO high.
 *
 * The board wiring does not match the historical CubeMX signal labels:
 * physical L3 -> L1_Pin, physical L2 -> M_Pin,
 * physical L1 -> L2_Pin, physical M -> L3_Pin.
 * Array order defines gray_active_mask bit3..bit0 as M, L1, L2, L3. */
static const GraySensor_TypeDef gray_sensors[GRAY_SENSOR_COUNT] =
{
  {L1_GPIO_Port, L1_Pin,  3000},
  {M_GPIO_Port,  M_Pin,   1000},
  {L2_GPIO_Port, L2_Pin, -1000},
  {L3_GPIO_Port, L3_Pin, -3000}
};

static DRV8870_HandleTypeDef *left_motor_handle;
static DRV8870_HandleTypeDef *right_motor_handle;
static TIM_HandleTypeDef *left_encoder_handle;
static TIM_HandleTypeDef *right_encoder_handle;
static ICM45686_HandleTypeDef *imu_handle;
static uint32_t left_encoder_previous;
static uint32_t right_encoder_previous;
static PID_HandleTypeDef steering_pid;
static PID_HandleTypeDef left_speed_pid;
static PID_HandleTypeDef right_speed_pid;
static LineFollower_StateTypeDef control_state;
static int16_t last_line_position;
static uint8_t control_initialized;
static uint8_t control_enabled;
static LineFollower_ModeTypeDef control_mode;

static uint8_t LineFollower_IsPIDConfigValid(
    const LineFollower_PIDConfigTypeDef *config)
{
  if (config == NULL)
  {
    return 0U;
  }

  return (uint8_t)(isfinite(config->steering_kp) &&
                   isfinite(config->steering_ki) &&
                   isfinite(config->steering_kd) &&
                   isfinite(config->speed_kp) &&
                   isfinite(config->speed_ki) &&
                   isfinite(config->speed_kd) &&
                   (config->steering_kp >= 0.0F) &&
                   (config->steering_kp <= 0.5F) &&
                   (config->steering_ki >= 0.0F) &&
                   (config->steering_ki <= 1.0F) &&
                   (config->steering_kd >= 0.0F) &&
                   (config->steering_kd <= 10.0F) &&
                   (config->speed_kp >= 0.0F) &&
                   (config->speed_kp <= 2000.0F) &&
                   (config->speed_ki >= 0.0F) &&
                   (config->speed_ki <= 10000.0F) &&
                   (config->speed_kd >= 0.0F) &&
                   (config->speed_kd <= 100.0F));
}

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

static void LineFollower_ReadGray(int16_t *position)
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
    return;
  }

  *position = (int16_t)(weighted_sum / active_count);
}

static uint8_t LineFollower_IsValidGrayPattern(uint8_t active_mask)
{
  switch (active_mask)
  {
    case 0x01U:
    case 0x02U:
    case 0x03U:
    case 0x04U:
    case 0x06U:
    case 0x07U:
    case 0x08U:
    case 0x0CU:
    case 0x0EU:
    case 0x0FU:
      return 1U;

    default:
      return 0U;
  }
}

static uint8_t LineFollower_IsStopMarker(uint8_t active_mask)
{
  return (uint8_t)((active_mask == GRAY_STOP_LEFT_MASK) ||
                   (active_mask == GRAY_STOP_RIGHT_MASK) ||
                   (active_mask == GRAY_STOP_ALL_MASK));
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
                                    TIM_HandleTypeDef *right_encoder,
                                    ICM45686_HandleTypeDef *imu)
{
  const float steering_effective_limit =
      LINE_FOLLOW_BASE_SPEED_TICKS + LINE_FOLLOW_MAX_SPEED_TICKS;

  if ((left_motor == NULL) || (right_motor == NULL) ||
      (left_encoder == NULL) || (right_encoder == NULL) ||
      (imu == NULL) || (imu->initialized == 0U))
  {
    return HAL_ERROR;
  }

  left_motor_handle = left_motor;
  right_motor_handle = right_motor;
  left_encoder_handle = left_encoder;
  right_encoder_handle = right_encoder;
  imu_handle = imu;

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
           -steering_effective_limit,
           steering_effective_limit,
           -steering_effective_limit,
           steering_effective_limit,
           0.02F);
  PID_Init(&left_speed_pid,
           LINE_FOLLOW_SPEED_KP,
           LINE_FOLLOW_SPEED_KI,
           LINE_FOLLOW_SPEED_KD,
           -LINE_FOLLOW_TEST_PWM_LIMIT,
           LINE_FOLLOW_TEST_PWM_LIMIT,
           -400.0F,
           400.0F,
           0.01F);
  PID_Init(&right_speed_pid,
           LINE_FOLLOW_SPEED_KP,
           LINE_FOLLOW_SPEED_KI,
           LINE_FOLLOW_SPEED_KD,
           -LINE_FOLLOW_TEST_PWM_LIMIT,
           LINE_FOLLOW_TEST_PWM_LIMIT,
           -400.0F,
           400.0F,
           0.01F);

  control_state = (LineFollower_StateTypeDef){0};
  last_line_position = 0;
  control_enabled = 0U;
  control_mode = LINE_FOLLOWER_MODE_FOLLOW;
  control_initialized = 1U;
  (void)DRV8870_Brake(left_motor_handle);
  (void)DRV8870_Brake(right_motor_handle);
  return HAL_OK;
}

static void LineFollower_StartMode(LineFollower_ModeTypeDef mode)
{
  if (control_initialized == 0U)
  {
    return;
  }

  left_encoder_previous = __HAL_TIM_GET_COUNTER(left_encoder_handle);
  right_encoder_previous = __HAL_TIM_GET_COUNTER(right_encoder_handle);
  control_state.gray_invalid_cycles = 0U;
  control_state.line_lost_cycles = 0U;
  control_state.stop_marker_cycles = 0U;
  control_state.stop_marker_tick = 0U;
  control_state.gray_raw_mask = 0U;
  control_state.gray_active_mask = 0U;
  control_state.gray_active_count = 0U;
  control_state.line_detected = 0U;
  control_state.line_position = 0;
  control_state.left_target_speed = 0.0F;
  control_state.right_target_speed = 0.0F;
  control_state.yaw_rate_dps = 0.0F;
  control_state.yaw_angle_deg = 0.0F;
  last_line_position = 0;
  ICM45686_ResetYaw(imu_handle);
  LineFollower_ResetControllers();
  control_mode = mode;
  control_enabled = 1U;
}

void LineFollower_Start(void)
{
  LineFollower_StartMode(LINE_FOLLOWER_MODE_FOLLOW);
}

void LineFollower_StartStraight(void)
{
  LineFollower_StartMode(LINE_FOLLOWER_MODE_STRAIGHT);
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
  const ICM45686_DataTypeDef *imu_data;
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

  control_state.imu_ready =
      (uint8_t)(ICM45686_Update(imu_handle, dt_seconds) == HAL_OK);
  imu_data = ICM45686_GetData(imu_handle);
  if (imu_data != NULL)
  {
    control_state.yaw_rate_dps = imu_data->yaw_rate_dps;
    control_state.yaw_angle_deg = imu_data->yaw_angle_deg;
    control_state.imu_read_error_count = imu_data->read_error_count;
  }

  control_state.left_encoder_delta =
      LineFollower_ReadEncoderDelta(left_encoder_handle,
                                    &left_encoder_previous) *
      LINE_FOLLOW_LEFT_ENCODER_SIGN;
  control_state.right_encoder_delta =
      LineFollower_ReadEncoderDelta(right_encoder_handle,
                                    &right_encoder_previous) *
      LINE_FOLLOW_RIGHT_ENCODER_SIGN;

  if ((control_enabled != 0U) &&
      (control_mode == LINE_FOLLOWER_MODE_STRAIGHT))
  {
    left_target = LINE_FOLLOW_BASE_SPEED_TICKS;
    right_target = LINE_FOLLOW_BASE_SPEED_TICKS;
  }
  else
  {
    LineFollower_ReadGray(&line_position);
    control_state.line_detected =
        LineFollower_IsValidGrayPattern(control_state.gray_active_mask);

    /*
     * The solid stop line can only produce a contiguous group of three
     * sensors or all four sensors.
     */
    if (LineFollower_IsStopMarker(control_state.gray_active_mask) != 0U)
    {
      if (control_state.stop_marker_cycles < UINT16_MAX)
      {
        ++control_state.stop_marker_cycles;
      }
    }
    else
    {
      control_state.stop_marker_cycles = 0U;
    }

    if (control_state.line_detected != 0U)
    {
      control_state.gray_invalid_cycles = 0U;
      control_state.line_lost_cycles = 0U;
      last_line_position = line_position;
    }
    else
    {
      if (control_state.gray_active_mask == 0U)
      {
        control_state.gray_invalid_cycles = 0U;
        if (control_state.line_lost_cycles < UINT16_MAX)
        {
          ++control_state.line_lost_cycles;
        }
      }
      else
      {
        if (control_state.gray_invalid_cycles < UINT16_MAX)
        {
          ++control_state.gray_invalid_cycles;
        }
        if ((control_state.gray_invalid_cycles >
             LINE_FOLLOW_INVALID_HOLD_CYCLES) &&
            (control_state.line_lost_cycles < UINT16_MAX))
        {
          ++control_state.line_lost_cycles;
        }
      }
      line_position = last_line_position;
    }
    control_state.line_position = line_position;

    if (control_enabled == 0U)
    {
      return;
    }

    if (control_state.stop_marker_cycles >= LINE_FOLLOW_STOP_MARKER_CYCLES)
    {
      if (control_state.stop_marker_tick == 0U)
      {
        control_state.stop_marker_tick = HAL_GetTick();
      }
      LineFollower_Stop();
      return;
    }

    if (control_state.line_lost_cycles >= LINE_FOLLOW_LOST_STOP_CYCLES)
    {
      LineFollower_Stop();
      return;
    }

    if (control_state.line_detected != 0U)
    {
      steering_correction = PID_Update(&steering_pid,
                                       (float)line_position,
                                       dt_seconds);

      /*
       * The center lies between L2 and L1.  When only either center-adjacent
       * sensor sees the line, guarantee an immediate correction even if the
       * stored Kp is too small to produce a noticeable speed difference.
       */
      if ((control_state.gray_active_mask == GRAY_L2_ACTIVE_MASK) &&
          (steering_correction < LINE_FOLLOW_SIDE_MIN_STEERING_TICKS))
      {
        steering_correction = LINE_FOLLOW_SIDE_MIN_STEERING_TICKS;
      }
      else if ((control_state.gray_active_mask == GRAY_L1_ACTIVE_MASK) &&
               (steering_correction > -LINE_FOLLOW_SIDE_MIN_STEERING_TICKS))
      {
        steering_correction = -LINE_FOLLOW_SIDE_MIN_STEERING_TICKS;
      }

      left_target = LINE_FOLLOW_BASE_SPEED_TICKS - steering_correction;
      right_target = LINE_FOLLOW_BASE_SPEED_TICKS + steering_correction;
    }
    else
    {
      /*
       * Keep the last valid motion command while the line is temporarily
       * missing.  The wheel-speed PID loops remain active.
       */
      left_target = control_state.left_target_speed;
      right_target = control_state.right_target_speed;
    }
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

HAL_StatusTypeDef LineFollower_GetPIDConfig(
    LineFollower_PIDConfigTypeDef *config)
{
  uint32_t primask;

  if ((config == NULL) || (control_initialized == 0U))
  {
    return HAL_ERROR;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  config->steering_kp = steering_pid.kp;
  config->steering_ki = steering_pid.ki;
  config->steering_kd = steering_pid.kd;
  config->speed_kp = left_speed_pid.kp;
  config->speed_ki = left_speed_pid.ki;
  config->speed_kd = left_speed_pid.kd;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return HAL_OK;
}

HAL_StatusTypeDef LineFollower_SetPIDConfig(
    const LineFollower_PIDConfigTypeDef *config)
{
  uint32_t primask;

  if ((control_initialized == 0U) ||
      (LineFollower_IsPIDConfigValid(config) == 0U))
  {
    return HAL_ERROR;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  steering_pid.kp = config->steering_kp;
  steering_pid.ki = config->steering_ki;
  steering_pid.kd = config->steering_kd;
  left_speed_pid.kp = config->speed_kp;
  left_speed_pid.ki = config->speed_ki;
  left_speed_pid.kd = config->speed_kd;
  right_speed_pid.kp = config->speed_kp;
  right_speed_pid.ki = config->speed_ki;
  right_speed_pid.kd = config->speed_kd;
  LineFollower_ResetControllers();
  if (primask == 0U)
  {
    __enable_irq();
  }
  return HAL_OK;
}
