#include "interrupt.h"

#include "line_follower.h"
#include "tim.h"
#include "usart.h"

volatile uint32_t times = 0;

void UART4_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart4);
}

void TIM17_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim17);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM17)
  {
    LineFollower_Update();
    times++;
  }
}
