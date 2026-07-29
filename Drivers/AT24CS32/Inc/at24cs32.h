#ifndef H723ZG_AT24CS32_H
#define H723ZG_AT24CS32_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#include <stdint.h>

#define AT24CS32_CAPACITY_BYTES       4096U
#define AT24CS32_PAGE_SIZE_BYTES      32U
#define AT24CS32_DEFAULT_TIMEOUT_MS   20U

typedef enum
{
  AT24CS32_OK = 0,
  AT24CS32_ERROR_PARAMETER,
  AT24CS32_ERROR_RANGE,
  AT24CS32_ERROR_HAL
} AT24CS32_StatusTypeDef;

typedef struct
{
  I2C_HandleTypeDef *i2c;
  uint16_t device_address;
  uint32_t timeout_ms;
} AT24CS32_HandleTypeDef;

AT24CS32_StatusTypeDef AT24CS32_Init(AT24CS32_HandleTypeDef *device,
                                    I2C_HandleTypeDef *i2c,
                                    uint8_t address_pins);
AT24CS32_StatusTypeDef AT24CS32_IsReady(AT24CS32_HandleTypeDef *device);
AT24CS32_StatusTypeDef AT24CS32_Read(AT24CS32_HandleTypeDef *device,
                                    uint16_t address,
                                    void *data,
                                    uint16_t length);
AT24CS32_StatusTypeDef AT24CS32_Write(AT24CS32_HandleTypeDef *device,
                                     uint16_t address,
                                     const void *data,
                                     uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* H723ZG_AT24CS32_H */
