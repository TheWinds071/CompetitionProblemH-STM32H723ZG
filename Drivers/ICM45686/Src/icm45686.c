#include "icm45686.h"

#include <math.h>
#include <stddef.h>

#define ICM45686_REG_ACCEL_DATA_X1       0x00U
#define ICM45686_REG_PWR_MGMT0           0x10U
#define ICM45686_REG_INT1_STATUS0        0x19U
#define ICM45686_REG_ACCEL_CONFIG0       0x1BU
#define ICM45686_REG_GYRO_CONFIG0        0x1CU
#define ICM45686_REG_INTF_CONFIG1_OVRD   0x2DU
#define ICM45686_REG_WHO_AM_I            0x72U
#define ICM45686_REG_IREG_ADDR_HIGH      0x7CU
#define ICM45686_REG_MISC2               0x7FU

#define ICM45686_WHO_AM_I_VALUE          0xE9U
#define ICM45686_SPI_READ_BIT            0x80U
#define ICM45686_SPI4_OVERRIDE           0x30U
#define ICM45686_SOFT_RESET              0x02U
#define ICM45686_RESET_DONE              0x80U
#define ICM45686_ACCEL_CONFIG_4G_800HZ   0x36U
#define ICM45686_GYRO_CONFIG_500DPS_800HZ 0x36U
#define ICM45686_ACCEL_GYRO_LOW_NOISE    0x0FU

#define ICM45686_ACCEL_SCALE_G           (4.0F / 32768.0F)
#define ICM45686_GYRO_SCALE_DPS          (500.0F / 32768.0F)
#define ICM45686_SPI_TIMEOUT_MS          2U
#define ICM45686_SENSOR_DATA_SIZE        14U
#define ICM45686_PI                      3.14159265358979323846F
#define ICM45686_SREG_CTRL_ADDRESS       0xA267U
#define ICM45686_BIG_ENDIAN_DATA         0x02U

static HAL_StatusTypeDef ICM45686_ReadRegisters(
    ICM45686_HandleTypeDef *device,
    uint8_t register_address,
    uint8_t *data,
    uint16_t size)
{
  uint8_t tx_buffer[ICM45686_SENSOR_DATA_SIZE + 1U] = {0U};
  uint8_t rx_buffer[ICM45686_SENSOR_DATA_SIZE + 1U] = {0U};
  HAL_StatusTypeDef status;
  uint16_t index;

  if ((device == NULL) || (device->spi == NULL) ||
      (data == NULL) || (size == 0U) ||
      (size > ICM45686_SENSOR_DATA_SIZE))
  {
    return HAL_ERROR;
  }

  tx_buffer[0] = register_address | ICM45686_SPI_READ_BIT;
  HAL_GPIO_WritePin(device->cs_port, device->cs_pin, GPIO_PIN_RESET);
  status = HAL_SPI_TransmitReceive(device->spi,
                                  tx_buffer,
                                  rx_buffer,
                                  size + 1U,
                                  ICM45686_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(device->cs_port, device->cs_pin, GPIO_PIN_SET);

  if (status != HAL_OK)
  {
    return status;
  }

  for (index = 0U; index < size; ++index)
  {
    data[index] = rx_buffer[index + 1U];
  }
  return HAL_OK;
}

static HAL_StatusTypeDef ICM45686_WriteRegister(
    ICM45686_HandleTypeDef *device,
    uint8_t register_address,
    uint8_t value)
{
  uint8_t tx_buffer[2] = {register_address, value};
  HAL_StatusTypeDef status;

  if ((device == NULL) || (device->spi == NULL))
  {
    return HAL_ERROR;
  }

  HAL_GPIO_WritePin(device->cs_port, device->cs_pin, GPIO_PIN_RESET);
  status = HAL_SPI_Transmit(device->spi,
                            tx_buffer,
                            sizeof(tx_buffer),
                            ICM45686_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(device->cs_port, device->cs_pin, GPIO_PIN_SET);
  return status;
}

static HAL_StatusTypeDef ICM45686_SetBigEndianData(
    ICM45686_HandleTypeDef *device)
{
  uint8_t tx_buffer[4] =
  {
    ICM45686_REG_IREG_ADDR_HIGH,
    (uint8_t)(ICM45686_SREG_CTRL_ADDRESS >> 8U),
    (uint8_t)ICM45686_SREG_CTRL_ADDRESS,
    ICM45686_BIG_ENDIAN_DATA
  };
  HAL_StatusTypeDef status;

  HAL_GPIO_WritePin(device->cs_port, device->cs_pin, GPIO_PIN_RESET);
  status = HAL_SPI_Transmit(device->spi,
                            tx_buffer,
                            sizeof(tx_buffer),
                            ICM45686_SPI_TIMEOUT_MS);
  HAL_GPIO_WritePin(device->cs_port, device->cs_pin, GPIO_PIN_SET);

  /* Indirect register accesses require at least a 4 us gap. */
  HAL_Delay(1U);
  return status;
}

static int16_t ICM45686_ParseInt16(const uint8_t *bytes)
{
  return (int16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static float ICM45686_WrapAngle(float angle_degrees)
{
  while (angle_degrees > 180.0F)
  {
    angle_degrees -= 360.0F;
  }
  while (angle_degrees <= -180.0F)
  {
    angle_degrees += 360.0F;
  }
  return angle_degrees;
}

static HAL_StatusTypeDef ICM45686_ReadAndParse(
    ICM45686_HandleTypeDef *device)
{
  uint8_t registers[ICM45686_SENSOR_DATA_SIZE];
  uint8_t axis;

  if (ICM45686_ReadRegisters(device,
                             ICM45686_REG_ACCEL_DATA_X1,
                             registers,
                             sizeof(registers)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  for (axis = 0U; axis < 3U; ++axis)
  {
    device->data.accel_raw[axis] =
        ICM45686_ParseInt16(&registers[axis * 2U]);
    device->data.gyro_raw[axis] =
        ICM45686_ParseInt16(&registers[6U + axis * 2U]);
    device->data.accel_g[axis] =
        (float)device->data.accel_raw[axis] * ICM45686_ACCEL_SCALE_G;
    device->data.gyro_dps[axis] =
        (float)device->data.gyro_raw[axis] * ICM45686_GYRO_SCALE_DPS;
  }

  device->data.temperature_raw = ICM45686_ParseInt16(&registers[12]);
  device->data.temperature_c =
      25.0F + ((float)device->data.temperature_raw / 128.0F);
  return HAL_OK;
}

HAL_StatusTypeDef ICM45686_Init(ICM45686_HandleTypeDef *device,
                                SPI_HandleTypeDef *spi,
                                GPIO_TypeDef *cs_port,
                                uint16_t cs_pin)
{
  uint8_t value;

  if ((device == NULL) || (spi == NULL) || (cs_port == NULL))
  {
    return HAL_ERROR;
  }

  *device = (ICM45686_HandleTypeDef){0};
  device->spi = spi;
  device->cs_port = cs_port;
  device->cs_pin = cs_pin;
  HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);
  HAL_Delay(3U);

  if (ICM45686_WriteRegister(device,
                             ICM45686_REG_INTF_CONFIG1_OVRD,
                             ICM45686_SPI4_OVERRIDE) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if ((ICM45686_ReadRegisters(device,
                              ICM45686_REG_WHO_AM_I,
                              &value,
                              1U) != HAL_OK) ||
      (value != ICM45686_WHO_AM_I_VALUE))
  {
    return HAL_ERROR;
  }

  if (ICM45686_WriteRegister(device,
                             ICM45686_REG_MISC2,
                             ICM45686_SOFT_RESET) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(2U);

  if ((ICM45686_WriteRegister(device,
                              ICM45686_REG_INTF_CONFIG1_OVRD,
                              ICM45686_SPI4_OVERRIDE) != HAL_OK) ||
      (ICM45686_ReadRegisters(device,
                              ICM45686_REG_INT1_STATUS0,
                              &value,
                              1U) != HAL_OK) ||
      ((value & ICM45686_RESET_DONE) == 0U))
  {
    return HAL_ERROR;
  }

  if ((ICM45686_SetBigEndianData(device) != HAL_OK) ||
      (ICM45686_WriteRegister(device,
                              ICM45686_REG_ACCEL_CONFIG0,
                              ICM45686_ACCEL_CONFIG_4G_800HZ) != HAL_OK) ||
      (ICM45686_WriteRegister(device,
                              ICM45686_REG_GYRO_CONFIG0,
                              ICM45686_GYRO_CONFIG_500DPS_800HZ) != HAL_OK) ||
      (ICM45686_WriteRegister(device,
                              ICM45686_REG_PWR_MGMT0,
                              ICM45686_ACCEL_GYRO_LOW_NOISE) != HAL_OK))
  {
    return HAL_ERROR;
  }

  HAL_Delay(100U);
  device->initialized = 1U;
  if (ICM45686_CalibrateGyro(device) != HAL_OK)
  {
    device->initialized = 0U;
    return HAL_ERROR;
  }

  ICM45686_ResetYaw(device);
  return HAL_OK;
}

HAL_StatusTypeDef ICM45686_CalibrateGyro(ICM45686_HandleTypeDef *device)
{
  float sums[3] = {0.0F, 0.0F, 0.0F};
  uint16_t sample;
  uint8_t axis;

  if ((device == NULL) || (device->initialized == 0U))
  {
    return HAL_ERROR;
  }

  for (sample = 0U; sample < ICM45686_CALIBRATION_SAMPLES; ++sample)
  {
    if (ICM45686_ReadAndParse(device) != HAL_OK)
    {
      return HAL_ERROR;
    }

    for (axis = 0U; axis < 3U; ++axis)
    {
      sums[axis] += device->data.gyro_dps[axis];
    }
    HAL_Delay(2U);
  }

  for (axis = 0U; axis < 3U; ++axis)
  {
    device->gyro_bias_dps[axis] =
        sums[axis] / (float)ICM45686_CALIBRATION_SAMPLES;
  }
  return HAL_OK;
}

HAL_StatusTypeDef ICM45686_Update(ICM45686_HandleTypeDef *device,
                                  float dt_seconds)
{
  const float time_constant =
      1.0F / (2.0F * ICM45686_PI * ICM45686_GYRO_FILTER_CUTOFF_HZ);
  float yaw_rate;
  float alpha;

  if ((device == NULL) || (device->initialized == 0U) ||
      !isfinite(dt_seconds) || (dt_seconds <= 0.0F))
  {
    return HAL_ERROR;
  }

  if (ICM45686_ReadAndParse(device) != HAL_OK)
  {
    if (device->data.read_error_count < UINT32_MAX)
    {
      ++device->data.read_error_count;
    }
    return HAL_ERROR;
  }

  yaw_rate =
      (device->data.gyro_dps[ICM45686_YAW_AXIS] -
       device->gyro_bias_dps[ICM45686_YAW_AXIS]) *
      ICM45686_YAW_AXIS_SIGN;
  alpha = dt_seconds / (time_constant + dt_seconds);
  device->filtered_yaw_rate_dps +=
      alpha * (yaw_rate - device->filtered_yaw_rate_dps);

  if (fabsf(device->filtered_yaw_rate_dps) <
      ICM45686_GYRO_DEADBAND_DPS)
  {
    device->data.yaw_rate_dps = 0.0F;
  }
  else
  {
    device->data.yaw_rate_dps = device->filtered_yaw_rate_dps;
  }

  device->data.yaw_angle_deg = ICM45686_WrapAngle(
      device->data.yaw_angle_deg +
      device->data.yaw_rate_dps * dt_seconds);
  if (device->data.sample_count < UINT32_MAX)
  {
    ++device->data.sample_count;
  }
  return HAL_OK;
}

void ICM45686_ResetYaw(ICM45686_HandleTypeDef *device)
{
  if (device == NULL)
  {
    return;
  }

  device->filtered_yaw_rate_dps = 0.0F;
  device->data.yaw_rate_dps = 0.0F;
  device->data.yaw_angle_deg = 0.0F;
}

const ICM45686_DataTypeDef *ICM45686_GetData(
    const ICM45686_HandleTypeDef *device)
{
  if ((device == NULL) || (device->initialized == 0U))
  {
    return NULL;
  }
  return &device->data;
}
