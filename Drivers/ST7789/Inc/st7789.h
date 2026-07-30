#ifndef H723ZG_ST7789_H
#define H723ZG_ST7789_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#include <stdint.h>

#define ST7789_WIDTH   172U
#define ST7789_HEIGHT  320U

#define ST7789_RGB565(red, green, blue) \
  ((uint16_t)((((uint16_t)(red) & 0xF8U) << 8U) | \
              (((uint16_t)(green) & 0xFCU) << 3U) | \
              ((uint16_t)(blue) >> 3U)))

#define ST7789_COLOR_BLACK  0x0000U
#define ST7789_COLOR_WHITE  0xFFFFU
#define ST7789_COLOR_RED    0xF800U
#define ST7789_COLOR_GREEN  0x07E0U
#define ST7789_COLOR_BLUE   0x001FU

HAL_StatusTypeDef ST7789_Init(SPI_HandleTypeDef *spi);

HAL_StatusTypeDef ST7789_FillScreen_DMA(uint16_t color);
HAL_StatusTypeDef ST7789_FillRect_DMA(uint16_t x,
                                     uint16_t y,
                                     uint16_t width,
                                     uint16_t height,
                                     uint16_t color);

/*
 * The RGB565 source is in CPU-endian uint16_t format and must remain valid
 * until ST7789_Wait() succeeds or ST7789_IsBusy() returns zero.
 */
HAL_StatusTypeDef ST7789_DrawRGB565_DMA(uint16_t x,
                                       uint16_t y,
                                       uint16_t width,
                                       uint16_t height,
                                       const uint16_t *pixels);

uint8_t ST7789_IsBusy(void);
HAL_StatusTypeDef ST7789_Wait(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* H723ZG_ST7789_H */
