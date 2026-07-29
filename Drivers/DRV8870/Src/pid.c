#include "pid.h"

#include <stddef.h>

static float PID_Clamp(float value, float minimum, float maximum)
{
  if (value > maximum)
  {
    return maximum;
  }
  if (value < minimum)
  {
    return minimum;
  }
  return value;
}

void PID_Init(PID_HandleTypeDef *pid,
              float kp,
              float ki,
              float kd,
              float output_min,
              float output_max,
              float integral_min,
              float integral_max,
              float derivative_tau)
{
  if (pid == NULL)
  {
    return;
  }

  pid->kp = kp;
  pid->ki = ki;
  pid->kd = kd;
  pid->output_min = output_min;
  pid->output_max = output_max;
  pid->integral_min = integral_min;
  pid->integral_max = integral_max;
  pid->derivative_tau = derivative_tau;
  PID_Reset(pid);
}

void PID_Reset(PID_HandleTypeDef *pid)
{
  if (pid == NULL)
  {
    return;
  }

  pid->integral = 0.0F;
  pid->previous_error = 0.0F;
  pid->derivative = 0.0F;
  pid->initialized = 0U;
}

float PID_Update(PID_HandleTypeDef *pid, float error, float dt_seconds)
{
  float derivative_raw;
  float derivative_alpha;
  float integral_candidate;
  float output;
  float output_clamped;

  if ((pid == NULL) || (dt_seconds <= 0.0F))
  {
    return 0.0F;
  }

  if (pid->initialized == 0U)
  {
    pid->previous_error = error;
    pid->initialized = 1U;
  }

  derivative_raw = (error - pid->previous_error) / dt_seconds;
  if (pid->derivative_tau > 0.0F)
  {
    derivative_alpha = dt_seconds / (pid->derivative_tau + dt_seconds);
    pid->derivative += derivative_alpha * (derivative_raw - pid->derivative);
  }
  else
  {
    pid->derivative = derivative_raw;
  }

  integral_candidate = PID_Clamp(pid->integral +
                                 (pid->ki * error * dt_seconds),
                                 pid->integral_min,
                                 pid->integral_max);
  output = (pid->kp * error) + integral_candidate +
           (pid->kd * pid->derivative);
  output_clamped = PID_Clamp(output, pid->output_min, pid->output_max);

  /* Stop integrating farther into saturation, but allow recovery. */
  if ((output == output_clamped) ||
      ((output > pid->output_max) && (error < 0.0F)) ||
      ((output < pid->output_min) && (error > 0.0F)))
  {
    pid->integral = integral_candidate;
  }

  pid->previous_error = error;
  return output_clamped;
}