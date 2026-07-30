#ifndef H723ZG_TASK_UI_H
#define H723ZG_TASK_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#include <stdint.h>

#define TASK_UI_TASK_COUNT  6U

HAL_StatusTypeDef TaskUI_Init(void);

/*
 * Polls the three active-low buttons and updates the display.
 * Returns 1 once for each debounced press of Button3 and writes a zero-based
 * task index to selected_task. Button1/2 move the selection up/down.
 */
uint8_t TaskUI_Process(uint8_t *selected_task);

uint8_t TaskUI_GetSelection(void);

#ifdef __cplusplus
}
#endif

#endif /* H723ZG_TASK_UI_H */
