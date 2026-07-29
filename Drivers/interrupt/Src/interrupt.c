#include "interrupt.h"

#include "line_follower.h"
#include "tim.h"

void TIM17_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim17);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM17)
  {
    LineFollower_Update();
  }
}