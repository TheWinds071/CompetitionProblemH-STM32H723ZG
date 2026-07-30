#ifndef ICM45686_H
#define ICM45686_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

#include <stdint.h>

/*
 * ICM-45686 is configured for +/-4 g, +/-500 dps and 800 Hz output.
 * The vehicle yaw axis is Z by default. Set the sign to -1 if a clockwise
 * rotation produces a positive reading on the assembled board.
 */
#define ICM45686_YAW_AXIS                 2U
#define ICM45686_YAW_AXIS_SIGN            1.0F
#define ICM45686_GYRO_FILTER_CUTOFF_HZ    20.0F
#define ICM45686_GYRO_DEADBAND_DPS        0.10F
#define ICM45686_CALIBRATION_SAMPLES      512U

typedef struct
{
  int16_t accel_raw[3];
  int16_t gyro_raw[3];
  int16_t temperature_raw;
  float accel_g[3];
  float gyro_dps[3];
  float temperature_c;
  float yaw_rate_dps;
  float yaw_angle_deg;
  uint32_t sample_count;
  uint32_t read_error_count;
} ICM45686_DataTypeDef;

typedef struct
{
  SPI_HandleTypeDef *spi;
  GPIO_TypeDef *cs_port;
  uint16_t cs_pin;
  float gyro_bias_dps[3];
  float filtered_yaw_rate_dps;
  ICM45686_DataTypeDef data;
  uint8_t initialized;
} ICM45686_HandleTypeDef;

HAL_StatusTypeDef ICM45686_Init(ICM45686_HandleTypeDef *device,
                                SPI_HandleTypeDef *spi,
                                GPIO_TypeDef *cs_port,
                                uint16_t cs_pin);
HAL_StatusTypeDef ICM45686_CalibrateGyro(ICM45686_HandleTypeDef *device);
HAL_StatusTypeDef ICM45686_Update(ICM45686_HandleTypeDef *device,
                                  float dt_seconds);
void ICM45686_ResetYaw(ICM45686_HandleTypeDef *device);
const ICM45686_DataTypeDef *ICM45686_GetData(
    const ICM45686_HandleTypeDef *device);

#ifdef __cplusplus
}
#endif

#endif /* ICM45686_H */
