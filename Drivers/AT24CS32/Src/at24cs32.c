#include "at24cs32.h"

#include <stddef.h>

#define AT24CS32_BASE_ADDRESS_7BIT  0x50U
#define AT24CS32_READY_TRIALS       10U
#define AT24CS32_WRITE_TIMEOUT_MS   10U
#define AT24CS32_WRITE_POLL_MS      1U

static uint8_t AT24CS32_IsRangeValid(uint16_t address, uint16_t length)
{
  return ((uint32_t)address + (uint32_t)length) <=
         AT24CS32_CAPACITY_BYTES;
}

static AT24CS32_StatusTypeDef AT24CS32_CheckDevice(
    const AT24CS32_HandleTypeDef *device)
{
  if ((device == NULL) || (device->i2c == NULL))
  {
    return AT24CS32_ERROR_PARAMETER;
  }
  return AT24CS32_OK;
}

static AT24CS32_StatusTypeDef AT24CS32_WaitWriteComplete(
    AT24CS32_HandleTypeDef *device)
{
  uint32_t tick_start = HAL_GetTick();

  while ((HAL_GetTick() - tick_start) < AT24CS32_WRITE_TIMEOUT_MS)
  {
    if (HAL_I2C_IsDeviceReady(device->i2c,
                              device->device_address,
                              1U,
                              device->timeout_ms) == HAL_OK)
    {
      return AT24CS32_OK;
    }
    HAL_Delay(AT24CS32_WRITE_POLL_MS);
  }
  return AT24CS32_ERROR_HAL;
}

AT24CS32_StatusTypeDef AT24CS32_Init(AT24CS32_HandleTypeDef *device,
                                    I2C_HandleTypeDef *i2c,
                                    uint8_t address_pins)
{
  if ((device == NULL) || (i2c == NULL) || (address_pins > 7U))
  {
    return AT24CS32_ERROR_PARAMETER;
  }

  device->i2c = i2c;
  device->device_address =
      (uint16_t)((AT24CS32_BASE_ADDRESS_7BIT | address_pins) << 1U);
  device->timeout_ms = AT24CS32_DEFAULT_TIMEOUT_MS;
  return AT24CS32_OK;
}

AT24CS32_StatusTypeDef AT24CS32_IsReady(AT24CS32_HandleTypeDef *device)
{
  if (AT24CS32_CheckDevice(device) != AT24CS32_OK)
  {
    return AT24CS32_ERROR_PARAMETER;
  }

  if (HAL_I2C_IsDeviceReady(device->i2c,
                            device->device_address,
                            AT24CS32_READY_TRIALS,
                            device->timeout_ms) != HAL_OK)
  {
    return AT24CS32_ERROR_HAL;
  }
  return AT24CS32_OK;
}

AT24CS32_StatusTypeDef AT24CS32_Read(AT24CS32_HandleTypeDef *device,
                                    uint16_t address,
                                    void *data,
                                    uint16_t length)
{
  if ((AT24CS32_CheckDevice(device) != AT24CS32_OK) ||
      ((data == NULL) && (length != 0U)))
  {
    return AT24CS32_ERROR_PARAMETER;
  }
  if (AT24CS32_IsRangeValid(address, length) == 0U)
  {
    return AT24CS32_ERROR_RANGE;
  }
  if (length == 0U)
  {
    return AT24CS32_OK;
  }

  if (HAL_I2C_Mem_Read(device->i2c,
                       device->device_address,
                       address,
                       I2C_MEMADD_SIZE_16BIT,
                       (uint8_t *)data,
                       length,
                       device->timeout_ms) != HAL_OK)
  {
    return AT24CS32_ERROR_HAL;
  }
  return AT24CS32_OK;
}

AT24CS32_StatusTypeDef AT24CS32_Write(AT24CS32_HandleTypeDef *device,
                                     uint16_t address,
                                     const void *data,
                                     uint16_t length)
{
  const uint8_t *source = (const uint8_t *)data;

  if ((AT24CS32_CheckDevice(device) != AT24CS32_OK) ||
      ((data == NULL) && (length != 0U)))
  {
    return AT24CS32_ERROR_PARAMETER;
  }
  if (AT24CS32_IsRangeValid(address, length) == 0U)
  {
    return AT24CS32_ERROR_RANGE;
  }

  while (length != 0U)
  {
    uint16_t page_remaining = (uint16_t)(AT24CS32_PAGE_SIZE_BYTES -
        (address % AT24CS32_PAGE_SIZE_BYTES));
    uint16_t chunk = (length < page_remaining) ? length : page_remaining;

    if (HAL_I2C_Mem_Write(device->i2c,
                          device->device_address,
                          address,
                          I2C_MEMADD_SIZE_16BIT,
                          (uint8_t *)source,
                          chunk,
                          device->timeout_ms) != HAL_OK)
    {
      return AT24CS32_ERROR_HAL;
    }
    if (AT24CS32_WaitWriteComplete(device) != AT24CS32_OK)
    {
      return AT24CS32_ERROR_HAL;
    }

    address = (uint16_t)(address + chunk);
    source += chunk;
    length = (uint16_t)(length - chunk);
  }
  return AT24CS32_OK;
}
