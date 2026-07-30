#ifndef H723ZG_TASK_UI_H
#define H723ZG_TASK_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#include <stdint.h>

#define TASK_UI_TASK_COUNT  6U

typedef enum
{
  TASK_UI_EVENT_NONE = 0U,
  TASK_UI_EVENT_START,
  TASK_UI_EVENT_EXIT
} TaskUI_EventTypeDef;

HAL_StatusTypeDef TaskUI_Init(void);

/*
 * Polls the three active-low buttons and updates the display.
 * In the selection screen, Button1/2 move the selection and Button3 starts it.
 * In the task screen, Button3 exits the task and returns to the selection.
 */
TaskUI_EventTypeDef TaskUI_Process(uint8_t *selected_task);

uint8_t TaskUI_GetSelection(void);
void TaskUI_StopwatchStopAt(uint32_t stop_tick);

#ifdef __cplusplus
}
#endif

#endif /* H723ZG_TASK_UI_H */
