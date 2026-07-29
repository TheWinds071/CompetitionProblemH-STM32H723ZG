#ifndef H723ZG_ESP32_PID_PROTOCOL_H
#define H723ZG_ESP32_PID_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

HAL_StatusTypeDef ESP32PID_Init(UART_HandleTypeDef *uart);
void ESP32PID_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* H723ZG_ESP32_PID_PROTOCOL_H */
