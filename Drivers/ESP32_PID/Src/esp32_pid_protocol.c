#include "esp32_pid_protocol.h"

#include "line_follower.h"
#include "pid_storage.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP32PID_RX_BUFFER_SIZE  256U
#define ESP32PID_LINE_SIZE       192U
#define ESP32PID_TOKEN_COUNT     10U
#define ESP32PID_TX_TIMEOUT_MS   50U

static UART_HandleTypeDef *protocol_uart;
static uint8_t receive_byte;
static volatile uint16_t receive_head;
static volatile uint16_t receive_tail;
static volatile uint8_t receive_overflow;
static uint8_t receive_buffer[ESP32PID_RX_BUFFER_SIZE];
static char line_buffer[ESP32PID_LINE_SIZE];
static size_t line_length;
static uint8_t discard_line;

static void ESP32PID_Send(const char *message)
{
  if ((protocol_uart != NULL) && (message != NULL))
  {
    (void)HAL_UART_Transmit(protocol_uart,
                            (const uint8_t *)message,
                            (uint16_t)strlen(message),
                            ESP32PID_TX_TIMEOUT_MS);
  }
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
  char response[ESP32PID_LINE_SIZE];
  size_t offset;
  float values[6];
  uint8_t index;

  if (LineFollower_GetPIDConfig(&config) != HAL_OK)
  {
    ESP32PID_Send("ERR,NOT_READY\r\n");
    return;
  }

  values[0] = config.steering_kp;
  values[1] = config.steering_ki;
  values[2] = config.steering_kd;
  values[3] = config.speed_kp;
  values[4] = config.speed_ki;
  values[5] = config.speed_kd;

  offset = (size_t)snprintf(response, sizeof(response), "OK,PID,ALL,");
  for (index = 0U; index < 6U; ++index)
  {
    offset = ESP32PID_AppendFloat(response,
                                 sizeof(response),
                                 offset,
                                 values[index]);
    if (offset >= sizeof(response))
    {
      ESP32PID_Send("ERR,INTERNAL\r\n");
      return;
    }
    response[offset++] = (index == 5U) ? '\r' : ',';
  }
  response[offset++] = '\n';
  response[offset] = '\0';
  ESP32PID_Send(response);
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
    ESP32PID_Send("ERR,NOT_READY\r\n");
    return;
  }
  if (LineFollower_SetPIDConfig(candidate) != HAL_OK)
  {
    ESP32PID_Send("ERR,PID_RANGE\r\n");
    return;
  }
  if (PIDStorage_Save(candidate) != PID_STORAGE_OK)
  {
    (void)LineFollower_SetPIDConfig(&previous);
    ESP32PID_Send("ERR,EEPROM\r\n");
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
    ESP32PID_Send("ERR,NOT_READY\r\n");
    return;
  }

  if ((count == 6U) && (strcmp(tokens[2], "STEERING") == 0) &&
      (ESP32PID_ParseValues(tokens, 3U, 3U, values) != 0U))
  {
    config.steering_kp = values[0];
    config.steering_ki = values[1];
    config.steering_kd = values[2];
  }
  else if ((count == 6U) && (strcmp(tokens[2], "SPEED") == 0) &&
           (ESP32PID_ParseValues(tokens, 3U, 3U, values) != 0U))
  {
    config.speed_kp = values[0];
    config.speed_ki = values[1];
    config.speed_kd = values[2];
  }
  else if ((count == 9U) && (strcmp(tokens[2], "ALL") == 0) &&
           (ESP32PID_ParseValues(tokens, 3U, 6U, values) != 0U))
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
    ESP32PID_Send("ERR,BAD_ARGUMENT\r\n");
    return;
  }

  ESP32PID_ApplyAndSave(&config);
}

static void ESP32PID_HandleLine(char *line)
{
  char *tokens[ESP32PID_TOKEN_COUNT];
  uint8_t count = ESP32PID_Split(line, tokens);

  if ((count == 1U) && (strcmp(tokens[0], "PING") == 0))
  {
    ESP32PID_Send("OK,PONG\r\n");
  }
  else if ((count == 2U) && (strcmp(tokens[0], "PID") == 0) &&
           (strcmp(tokens[1], "GET") == 0))
  {
    ESP32PID_SendConfig();
  }
  else if ((count >= 3U) && (strcmp(tokens[0], "PID") == 0) &&
           (strcmp(tokens[1], "SET") == 0))
  {
    ESP32PID_HandleSet(tokens, count);
  }
  else if ((count == 2U) && (strcmp(tokens[0], "PID") == 0) &&
           (strcmp(tokens[1], "LOAD") == 0))
  {
    LineFollower_PIDConfigTypeDef config;
    if ((PIDStorage_Load(&config) != PID_STORAGE_OK) ||
        (LineFollower_SetPIDConfig(&config) != HAL_OK))
    {
      ESP32PID_Send("ERR,EEPROM\r\n");
    }
    else
    {
      ESP32PID_SendConfig();
    }
  }
  else if ((count == 2U) && (strcmp(tokens[0], "PID") == 0) &&
           (strcmp(tokens[1], "DEFAULT") == 0))
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
    ESP32PID_Send("ERR,BAD_COMMAND\r\n");
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
  line_length = 0U;
  discard_line = 0U;

  HAL_NVIC_SetPriority(UART4_IRQn, 5U, 0U);
  HAL_NVIC_EnableIRQ(UART4_IRQn);
  return HAL_UART_Receive_IT(protocol_uart, &receive_byte, 1U);
}

void ESP32PID_Process(void)
{
  if (receive_overflow != 0U)
  {
    receive_overflow = 0U;
    line_length = 0U;
    discard_line = 1U;
    ESP32PID_Send("ERR,RX_OVERFLOW\r\n");
  }

  while (receive_tail != receive_head)
  {
    uint8_t byte = receive_buffer[receive_tail];
    receive_tail = (uint16_t)((receive_tail + 1U) % ESP32PID_RX_BUFFER_SIZE);

    if (byte == '\n')
    {
      if ((discard_line == 0U) && (line_length != 0U))
      {
        if (line_buffer[line_length - 1U] == '\r')
        {
          --line_length;
        }
        line_buffer[line_length] = '\0';
        ESP32PID_HandleLine(line_buffer);
      }
      line_length = 0U;
      discard_line = 0U;
    }
    else if (discard_line == 0U)
    {
      if (line_length < (sizeof(line_buffer) - 1U))
      {
        line_buffer[line_length++] = (char)byte;
      }
      else
      {
        line_length = 0U;
        discard_line = 1U;
        ESP32PID_Send("ERR,LINE_TOO_LONG\r\n");
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
