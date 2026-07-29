#include "esp32_pid_protocol.h"

#include "line_follower.h"
#include "pid_storage.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP32PID_RX_BUFFER_SIZE  256U
#define ESP32PID_BODY_SIZE       192U
#define ESP32PID_TOKEN_COUNT     10U
#define ESP32PID_TX_TIMEOUT_MS   50U
#define ESP32PID_HEADER_0         0xAAU
#define ESP32PID_HEADER_1         0x55U
#define ESP32PID_TAIL_0           0x55U
#define ESP32PID_TAIL_1           0xAAU

typedef enum
{
  ESP32PID_WAIT_HEADER_0 = 0,
  ESP32PID_WAIT_HEADER_1,
  ESP32PID_RECEIVE_BODY
} ESP32PID_ReceiveStateTypeDef;

static UART_HandleTypeDef *protocol_uart;
static uint8_t receive_byte;
static volatile uint16_t receive_head;
static volatile uint16_t receive_tail;
static volatile uint8_t receive_overflow;
static uint8_t receive_buffer[ESP32PID_RX_BUFFER_SIZE];
static char body_buffer[ESP32PID_BODY_SIZE];
static size_t body_length;
static uint8_t discard_frame;
static uint8_t tail_pending;
static ESP32PID_ReceiveStateTypeDef receive_state;

static void ESP32PID_SendBody(const char *body)
{
  uint8_t frame[ESP32PID_BODY_SIZE + 4U];
  size_t body_size;

  if ((protocol_uart == NULL) || (body == NULL))
  {
    return;
  }

  body_size = strlen(body);
  if (body_size >= ESP32PID_BODY_SIZE)
  {
    return;
  }

  frame[0] = ESP32PID_HEADER_0;
  frame[1] = ESP32PID_HEADER_1;
  memcpy(&frame[2], body, body_size);
  frame[body_size + 2U] = ESP32PID_TAIL_0;
  frame[body_size + 3U] = ESP32PID_TAIL_1;
  (void)HAL_UART_Transmit(protocol_uart,
                          frame,
                          (uint16_t)(body_size + 4U),
                          ESP32PID_TX_TIMEOUT_MS);
}

static size_t ESP32PID_AppendFloat(char *buffer,
                                   size_t capacity,
                                   size_t offset,
                                   float value)
{
  uint32_t whole = (uint32_t)value;
  uint32_t fraction = (uint32_t)(((value - (float)whole) * 1000000.0F) +
                                 0.5F);
  int result;

  if (fraction >= 1000000U)
  {
    ++whole;
    fraction = 0U;
  }
  if (offset >= capacity)
  {
    return capacity;
  }
  result = snprintf(buffer + offset,
                    capacity - offset,
                    "%lu.%06lu",
                    (unsigned long)whole,
                    (unsigned long)fraction);
  if (result < 0)
  {
    return capacity;
  }
  if ((size_t)result >= (capacity - offset))
  {
    return capacity;
  }
  return offset + (size_t)result;
}

static void ESP32PID_SendConfig(void)
{
  LineFollower_PIDConfigTypeDef config;
  char response[ESP32PID_BODY_SIZE];
  size_t offset;
  float values[6];
  uint8_t index;

  if (LineFollower_GetPIDConfig(&config) != HAL_OK)
  {
    ESP32PID_SendBody("ERR,NOT_READY");
    return;
  }

  values[0] = config.steering_kp;
  values[1] = config.steering_ki;
  values[2] = config.steering_kd;
  values[3] = config.speed_kp;
  values[4] = config.speed_ki;
  values[5] = config.speed_kd;

  offset = (size_t)snprintf(response, sizeof(response), "PID,");
  for (index = 0U; index < 6U; ++index)
  {
    offset = ESP32PID_AppendFloat(response,
                                 sizeof(response),
                                 offset,
                                 values[index]);
    if (offset >= sizeof(response))
    {
      ESP32PID_SendBody("ERR,INTERNAL");
      return;
    }
    if (index != 5U)
    {
      response[offset++] = ',';
    }
  }
  response[offset] = '\0';
  ESP32PID_SendBody(response);
}

static char *ESP32PID_Trim(char *text)
{
  char *end;

  while ((*text == ' ') || (*text == '\t'))
  {
    ++text;
  }
  end = text + strlen(text);
  while ((end > text) && ((end[-1] == ' ') || (end[-1] == '\t')))
  {
    --end;
  }
  *end = '\0';
  return text;
}

static uint8_t ESP32PID_ParseFloat(const char *text, float *value)
{
  char *end;
  float parsed;

  if ((text == NULL) || (value == NULL) || (*text == '\0'))
  {
    return 0U;
  }
  parsed = strtof(text, &end);
  if ((*end != '\0') || !isfinite(parsed))
  {
    return 0U;
  }
  *value = parsed;
  return 1U;
}

static uint8_t ESP32PID_Split(char *line, char **tokens)
{
  uint8_t count = 0U;
  char *cursor = line;

  while ((cursor != NULL) && (count < ESP32PID_TOKEN_COUNT))
  {
    char *separator = strchr(cursor, ',');
    if (separator != NULL)
    {
      *separator = '\0';
    }
    tokens[count++] = ESP32PID_Trim(cursor);
    cursor = (separator != NULL) ? (separator + 1) : NULL;
  }
  if (cursor != NULL)
  {
    return 0U;
  }
  return count;
}

static uint8_t ESP32PID_ParseValues(char **tokens,
                                    uint8_t first,
                                    uint8_t count,
                                    float *values)
{
  uint8_t index;

  for (index = 0U; index < count; ++index)
  {
    if (ESP32PID_ParseFloat(tokens[first + index], &values[index]) == 0U)
    {
      return 0U;
    }
  }
  return 1U;
}

static void ESP32PID_ApplyAndSave(
    const LineFollower_PIDConfigTypeDef *candidate)
{
  LineFollower_PIDConfigTypeDef previous;

  if (LineFollower_GetPIDConfig(&previous) != HAL_OK)
  {
    ESP32PID_SendBody("ERR,NOT_READY");
    return;
  }
  if (LineFollower_SetPIDConfig(candidate) != HAL_OK)
  {
    ESP32PID_SendBody("ERR,PID_RANGE");
    return;
  }
  if (PIDStorage_Save(candidate) != PID_STORAGE_OK)
  {
    (void)LineFollower_SetPIDConfig(&previous);
    ESP32PID_SendBody("ERR,EEPROM");
    return;
  }
  ESP32PID_SendConfig();
}

static void ESP32PID_HandleSet(char **tokens, uint8_t count)
{
  LineFollower_PIDConfigTypeDef config;
  float values[6];

  if (LineFollower_GetPIDConfig(&config) != HAL_OK)
  {
    ESP32PID_SendBody("ERR,NOT_READY");
    return;
  }

  if ((count == 4U) && (strcmp(tokens[0], "TURN") == 0) &&
      (ESP32PID_ParseValues(tokens, 1U, 3U, values) != 0U))
  {
    config.steering_kp = values[0];
    config.steering_ki = values[1];
    config.steering_kd = values[2];
  }
  else if ((count == 4U) && (strcmp(tokens[0], "SPEED") == 0) &&
           (ESP32PID_ParseValues(tokens, 1U, 3U, values) != 0U))
  {
    config.speed_kp = values[0];
    config.speed_ki = values[1];
    config.speed_kd = values[2];
  }
  else if ((count == 7U) && (strcmp(tokens[0], "ALL") == 0) &&
           (ESP32PID_ParseValues(tokens, 1U, 6U, values) != 0U))
  {
    config.steering_kp = values[0];
    config.steering_ki = values[1];
    config.steering_kd = values[2];
    config.speed_kp = values[3];
    config.speed_ki = values[4];
    config.speed_kd = values[5];
  }
  else
  {
    ESP32PID_SendBody("ERR,BAD_ARGUMENT");
    return;
  }

  ESP32PID_ApplyAndSave(&config);
}

static void ESP32PID_HandleBody(char *body)
{
  char *tokens[ESP32PID_TOKEN_COUNT];
  uint8_t count = ESP32PID_Split(body, tokens);

  if ((count == 1U) && (strcmp(tokens[0], "PING") == 0))
  {
    ESP32PID_SendBody("PONG");
  }
  else if ((count == 1U) && (strcmp(tokens[0], "GET") == 0))
  {
    ESP32PID_SendConfig();
  }
  else if (((count == 4U) &&
            ((strcmp(tokens[0], "TURN") == 0) ||
             (strcmp(tokens[0], "SPEED") == 0))) ||
           ((count == 7U) && (strcmp(tokens[0], "ALL") == 0)))
  {
    ESP32PID_HandleSet(tokens, count);
  }
  else if ((count == 1U) && (strcmp(tokens[0], "LOAD") == 0))
  {
    LineFollower_PIDConfigTypeDef config;
    if ((PIDStorage_Load(&config) != PID_STORAGE_OK) ||
        (LineFollower_SetPIDConfig(&config) != HAL_OK))
    {
      ESP32PID_SendBody("ERR,EEPROM");
    }
    else
    {
      ESP32PID_SendConfig();
    }
  }
  else if ((count == 1U) && (strcmp(tokens[0], "DEFAULT") == 0))
  {
    const LineFollower_PIDConfigTypeDef defaults =
    {
      LINE_FOLLOW_STEERING_KP,
      LINE_FOLLOW_STEERING_KI,
      LINE_FOLLOW_STEERING_KD,
      LINE_FOLLOW_SPEED_KP,
      LINE_FOLLOW_SPEED_KI,
      LINE_FOLLOW_SPEED_KD
    };
    ESP32PID_ApplyAndSave(&defaults);
  }
  else
  {
    ESP32PID_SendBody("ERR,BAD_COMMAND");
  }
}

HAL_StatusTypeDef ESP32PID_Init(UART_HandleTypeDef *uart)
{
  if ((uart == NULL) || (uart->Instance != UART4))
  {
    return HAL_ERROR;
  }

  protocol_uart = uart;
  receive_head = 0U;
  receive_tail = 0U;
  receive_overflow = 0U;
  body_length = 0U;
  discard_frame = 0U;
  tail_pending = 0U;
  receive_state = ESP32PID_WAIT_HEADER_0;

  HAL_NVIC_SetPriority(UART4_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(UART4_IRQn);
  return HAL_UART_Receive_IT(protocol_uart, &receive_byte, 1U);
}

void ESP32PID_Process(void)
{
  if (receive_overflow != 0U)
  {
    receive_overflow = 0U;
    body_length = 0U;
    discard_frame = 0U;
    tail_pending = 0U;
    receive_state = ESP32PID_WAIT_HEADER_0;
    ESP32PID_SendBody("ERR,RX_OVERFLOW");
  }

  while (receive_tail != receive_head)
  {
    uint8_t byte = receive_buffer[receive_tail];
    receive_tail = (uint16_t)((receive_tail + 1U) % ESP32PID_RX_BUFFER_SIZE);

    if (receive_state == ESP32PID_WAIT_HEADER_0)
    {
      if (byte == ESP32PID_HEADER_0)
      {
        receive_state = ESP32PID_WAIT_HEADER_1;
      }
    }
    else if (receive_state == ESP32PID_WAIT_HEADER_1)
    {
      if (byte == ESP32PID_HEADER_1)
      {
        body_length = 0U;
        discard_frame = 0U;
        tail_pending = 0U;
        receive_state = ESP32PID_RECEIVE_BODY;
      }
      else if (byte != ESP32PID_HEADER_0)
      {
        receive_state = ESP32PID_WAIT_HEADER_0;
      }
    }
    else if (tail_pending != 0U)
    {
      if (byte == ESP32PID_TAIL_1)
      {
        if (discard_frame == 0U)
        {
          body_buffer[body_length] = '\0';
          ESP32PID_HandleBody(body_buffer);
        }
        body_length = 0U;
        discard_frame = 0U;
        tail_pending = 0U;
        receive_state = ESP32PID_WAIT_HEADER_0;
      }
      else
      {
        if (discard_frame == 0U)
        {
          if (body_length < (sizeof(body_buffer) - 1U))
          {
            body_buffer[body_length++] = (char)ESP32PID_TAIL_0;
          }
          else
          {
            discard_frame = 1U;
            ESP32PID_SendBody("ERR,FRAME_TOO_LONG");
          }
        }
        tail_pending = (uint8_t)(byte == ESP32PID_TAIL_0);
        if ((tail_pending == 0U) && (discard_frame == 0U))
        {
          if (body_length < (sizeof(body_buffer) - 1U))
          {
            body_buffer[body_length++] = (char)byte;
          }
          else
          {
            discard_frame = 1U;
            ESP32PID_SendBody("ERR,FRAME_TOO_LONG");
          }
        }
      }
    }
    else if (byte == ESP32PID_TAIL_0)
    {
      tail_pending = 1U;
    }
    else if (discard_frame == 0U)
    {
      if (body_length < (sizeof(body_buffer) - 1U))
      {
        body_buffer[body_length++] = (char)byte;
      }
      else
      {
        discard_frame = 1U;
        ESP32PID_SendBody("ERR,FRAME_TOO_LONG");
      }
    }
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
  if (uart == protocol_uart)
  {
    uint16_t next = (uint16_t)((receive_head + 1U) %
                               ESP32PID_RX_BUFFER_SIZE);
    if (next == receive_tail)
    {
      receive_overflow = 1U;
    }
    else
    {
      receive_buffer[receive_head] = receive_byte;
      receive_head = next;
    }
    (void)HAL_UART_Receive_IT(protocol_uart, &receive_byte, 1U);
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  if (uart == protocol_uart)
  {
    (void)HAL_UART_Receive_IT(protocol_uart, &receive_byte, 1U);
  }
}
