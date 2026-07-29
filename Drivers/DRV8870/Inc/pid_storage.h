#ifndef H723ZG_PID_STORAGE_H
#define H723ZG_PID_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at24cs32.h"
#include "line_follower.h"

typedef enum
{
  PID_STORAGE_OK = 0,
  PID_STORAGE_NOT_FOUND,
  PID_STORAGE_ERROR_PARAMETER,
  PID_STORAGE_ERROR_EEPROM,
  PID_STORAGE_ERROR_VERIFY
} PIDStorage_StatusTypeDef;

PIDStorage_StatusTypeDef PIDStorage_Init(I2C_HandleTypeDef *i2c);
PIDStorage_StatusTypeDef PIDStorage_Load(
    LineFollower_PIDConfigTypeDef *config);
PIDStorage_StatusTypeDef PIDStorage_Save(
    const LineFollower_PIDConfigTypeDef *config);

#ifdef __cplusplus
}
#endif

#endif /* H723ZG_PID_STORAGE_H */
