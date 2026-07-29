#ifndef H723ZG_PID_H
#define H723ZG_PID_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
  float kp;
  float ki;
  float kd;
  float output_min;
  float output_max;
  float integral_min;
  float integral_max;
  float derivative_tau;
  float integral;
  float previous_error;
  float derivative;
  uint8_t initialized;
} PID_HandleTypeDef;

void PID_Init(PID_HandleTypeDef *pid,
              float kp,
              float ki,
              float kd,
              float output_min,
              float output_max,
              float integral_min,
              float integral_max,
              float derivative_tau);
void PID_Reset(PID_HandleTypeDef *pid);
float PID_Update(PID_HandleTypeDef *pid, float error, float dt_seconds);

#ifdef __cplusplus
}
#endif

#endif /* H723ZG_PID_H */