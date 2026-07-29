#include "drv8870.h"

#include <stddef.h>

static uint8_t DRV8870_IsChannelValid(uint32_t channel)
{
  return (channel == TIM_CHANNEL_1) ||
         (channel == TIM_CHANNEL_2) ||
         (channel == TIM_CHANNEL_3) ||
         (channel == TIM_CHANNEL_4);
}

static DRV8870_StatusTypeDef DRV8870_CheckReady(
    const DRV8870_HandleTypeDef *driver)
{
  if (driver == NULL)
  {
    return DRV8870_ERROR_PARAMETER;
  }

  if ((driver->initialized == 0U) || (driver->timer == NULL))
  {
    return DRV8870_ERROR_NOT_INITIALIZED;
  }

  return DRV8870_OK;
}

static void DRV8870_SetCompare(const DRV8870_HandleTypeDef *driver,
                               uint32_t channel,
                               uint32_t compare)
{
  __HAL_TIM_SET_COMPARE(driver->timer, channel, compare);
}

DRV8870_StatusTypeDef DRV8870_Init(DRV8870_HandleTypeDef *driver,
                                    TIM_HandleTypeDef *timer,
                                    uint32_t in1_channel,
                                    uint32_t in2_channel)
{
  uint32_t auto_reload;

  if ((driver == NULL) || (timer == NULL) ||
      (in1_channel == in2_channel) ||
      (DRV8870_IsChannelValid(in1_channel) == 0U) ||
      (DRV8870_IsChannelValid(in2_channel) == 0U))
  {
    return DRV8870_ERROR_PARAMETER;
  }

  auto_reload = __HAL_TIM_GET_AUTORELOAD(timer);
  if (auto_reload == UINT32_MAX)
  {
    return DRV8870_ERROR_PARAMETER;
  }

  driver->timer = timer;
  driver->in1_channel = in1_channel;
  driver->in2_channel = in2_channel;
  driver->pwm_period = auto_reload + 1U;
  driver->speed = 0;
  driver->mode = DRV8870_MODE_COAST;
  driver->initialized = 0U;

  /*
   * Start both channels low. This keeps the bridge in coast while the two
   * timer outputs are enabled one after the other.
   */
  DRV8870_SetCompare(driver, in1_channel, 0U);
  DRV8870_SetCompare(driver, in2_channel, 0U);

  if (HAL_TIM_PWM_Start(timer, in1_channel) != HAL_OK)
  {
    return DRV8870_ERROR_HAL;
  }

  if (HAL_TIM_PWM_Start(timer, in2_channel) != HAL_OK)
  {
    (void)HAL_TIM_PWM_Stop(timer, in1_channel);
    return DRV8870_ERROR_HAL;
  }

  driver->initialized = 1U;
  return DRV8870_Brake(driver);
}

DRV8870_StatusTypeDef DRV8870_SetSpeed(DRV8870_HandleTypeDef *driver,
                                        int16_t speed)
{
  DRV8870_StatusTypeDef status;
  uint32_t magnitude;
  uint32_t drive_ticks;
  uint32_t brake_ticks;

  status = DRV8870_CheckReady(driver);
  if (status != DRV8870_OK)
  {
    return status;
  }

  if ((speed < -DRV8870_SPEED_MAX) || (speed > DRV8870_SPEED_MAX))
  {
    return DRV8870_ERROR_PARAMETER;
  }

  if (speed == 0)
  {
    return DRV8870_Brake(driver);
  }

  magnitude = (speed < 0) ? (uint32_t)(-(int32_t)speed) : (uint32_t)speed;
  drive_ticks = (uint32_t)((((uint64_t)driver->pwm_period * magnitude) +
                            (DRV8870_SPEED_MAX / 2U)) /
                           DRV8870_SPEED_MAX);
  brake_ticks = driver->pwm_period - drive_ticks;

  /*
   * PWM1 is high before the compare point. The fixed input is held high,
   * while the other input is high during the brake interval and low during
   * the drive interval. Thus the PWM off-time is always low-side slow decay.
   *
   * Write the new fixed-high side first. A direction change therefore passes
   * through brake (IN1 = IN2 = 1), never through an unintended reverse pulse.
   */
  if (speed > 0)
  {
    DRV8870_SetCompare(driver, driver->in1_channel, driver->pwm_period);
    DRV8870_SetCompare(driver, driver->in2_channel, brake_ticks);
    driver->mode = DRV8870_MODE_FORWARD;
  }
  else
  {
    DRV8870_SetCompare(driver, driver->in2_channel, driver->pwm_period);
    DRV8870_SetCompare(driver, driver->in1_channel, brake_ticks);
    driver->mode = DRV8870_MODE_REVERSE;
  }

  driver->speed = speed;
  return DRV8870_OK;
}

DRV8870_StatusTypeDef DRV8870_Brake(DRV8870_HandleTypeDef *driver)
{
  DRV8870_StatusTypeDef status = DRV8870_CheckReady(driver);

  if (status != DRV8870_OK)
  {
    return status;
  }

  DRV8870_SetCompare(driver, driver->in1_channel, driver->pwm_period);
  DRV8870_SetCompare(driver, driver->in2_channel, driver->pwm_period);
  driver->speed = 0;
  driver->mode = DRV8870_MODE_BRAKE;
  return DRV8870_OK;
}

DRV8870_StatusTypeDef DRV8870_Coast(DRV8870_HandleTypeDef *driver)
{
  DRV8870_StatusTypeDef status = DRV8870_CheckReady(driver);

  if (status != DRV8870_OK)
  {
    return status;
  }

  DRV8870_SetCompare(driver, driver->in1_channel, 0U);
  DRV8870_SetCompare(driver, driver->in2_channel, 0U);
  driver->speed = 0;
  driver->mode = DRV8870_MODE_COAST;
  return DRV8870_OK;
}

DRV8870_StatusTypeDef DRV8870_DeInit(DRV8870_HandleTypeDef *driver)
{
  DRV8870_StatusTypeDef status;
  HAL_StatusTypeDef in1_status;
  HAL_StatusTypeDef in2_status;

  status = DRV8870_CheckReady(driver);
  if (status != DRV8870_OK)
  {
    return status;
  }

  (void)DRV8870_Coast(driver);
  in1_status = HAL_TIM_PWM_Stop(driver->timer, driver->in1_channel);
  in2_status = HAL_TIM_PWM_Stop(driver->timer, driver->in2_channel);
  driver->initialized = 0U;

  if ((in1_status != HAL_OK) || (in2_status != HAL_OK))
  {
    return DRV8870_ERROR_HAL;
  }

  return DRV8870_OK;
}
