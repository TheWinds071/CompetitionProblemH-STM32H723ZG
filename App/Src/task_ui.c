#include "task_ui.h"

#include "main.h"
#include "st7789.h"

#include <stddef.h>

#define TASK_UI_DEBOUNCE_MS       30U
#define TASK_UI_RENDER_TIMEOUT_MS 1000U

#define TASK_UI_BACKGROUND  ST7789_RGB565(8U, 13U, 24U)
#define TASK_UI_HEADER      ST7789_RGB565(16U, 42U, 67U)
#define TASK_UI_ITEM        ST7789_RGB565(29U, 42U, 58U)
#define TASK_UI_SELECTED    ST7789_RGB565(255U, 177U, 59U)
#define TASK_UI_BORDER      ST7789_RGB565(65U, 86U, 110U)
#define TASK_UI_MUTED       ST7789_RGB565(148U, 163U, 184U)

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  uint8_t stable_pressed;
  uint8_t candidate_pressed;
  uint32_t candidate_since;
} TaskUI_ButtonTypeDef;

static TaskUI_ButtonTypeDef buttons[3] =
{
  {Button1_GPIO_Port, Button1_Pin, 0U, 0U, 0U},
  {Button2_GPIO_Port, Button2_Pin, 0U, 0U, 0U},
  {Button3_GPIO_Port, Button3_Pin, 0U, 0U, 0U}
};

__attribute__((section(".dma_buffer"), aligned(32)))
static uint16_t framebuffer[ST7789_WIDTH * ST7789_HEIGHT];

static uint8_t selected_index;
static uint8_t ui_initialized;

static void TaskUI_FillRect(uint16_t x,
                            uint16_t y,
                            uint16_t width,
                            uint16_t height,
                            uint16_t color)
{
  uint16_t row;
  uint16_t column;

  if (((uint32_t)x + width > ST7789_WIDTH) ||
      ((uint32_t)y + height > ST7789_HEIGHT))
  {
    return;
  }

  for (row = y; row < (uint16_t)(y + height); ++row)
  {
    uint32_t offset = (uint32_t)row * ST7789_WIDTH + x;
    for (column = 0U; column < width; ++column)
    {
      framebuffer[offset + column] = color;
    }
  }
}

static void TaskUI_DrawBorder(uint16_t x,
                              uint16_t y,
                              uint16_t width,
                              uint16_t height,
                              uint16_t color)
{
  TaskUI_FillRect(x, y, width, 1U, color);
  TaskUI_FillRect(x, (uint16_t)(y + height - 1U), width, 1U, color);
  TaskUI_FillRect(x, y, 1U, height, color);
  TaskUI_FillRect((uint16_t)(x + width - 1U), y, 1U, height, color);
}

static void TaskUI_GetGlyph(char character, uint8_t glyph[5])
{
  uint8_t index;
  static const uint8_t digits[10][5] =
  {
    {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU},
    {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U},
    {0x42U, 0x61U, 0x51U, 0x49U, 0x46U},
    {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U},
    {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U},
    {0x27U, 0x45U, 0x45U, 0x45U, 0x39U},
    {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U},
    {0x01U, 0x71U, 0x09U, 0x05U, 0x03U},
    {0x36U, 0x49U, 0x49U, 0x49U, 0x36U},
    {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU}
  };

  for (index = 0U; index < 5U; ++index)
  {
    glyph[index] = 0U;
  }

  if ((character >= '0') && (character <= '9'))
  {
    for (index = 0U; index < 5U; ++index)
    {
      glyph[index] = digits[(uint8_t)character - (uint8_t)'0'][index];
    }
    return;
  }

  switch (character)
  {
    case 'A': {const uint8_t v[5] = {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'B': {const uint8_t v[5] = {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'C': {const uint8_t v[5] = {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'D': {const uint8_t v[5] = {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'E': {const uint8_t v[5] = {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'I': {const uint8_t v[5] = {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'K': {const uint8_t v[5] = {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'L': {const uint8_t v[5] = {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'N': {const uint8_t v[5] = {0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'O': {const uint8_t v[5] = {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'P': {const uint8_t v[5] = {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'R': {const uint8_t v[5] = {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'S': {const uint8_t v[5] = {0x46U, 0x49U, 0x49U, 0x49U, 0x31U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'T': {const uint8_t v[5] = {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'U': {const uint8_t v[5] = {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'W': {const uint8_t v[5] = {0x3FU, 0x40U, 0x38U, 0x40U, 0x3FU}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case 'Y': {const uint8_t v[5] = {0x07U, 0x08U, 0x70U, 0x08U, 0x07U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    case ':': {const uint8_t v[5] = {0x00U, 0x36U, 0x36U, 0x00U, 0x00U}; for (index = 0U; index < 5U; ++index) glyph[index] = v[index]; break;}
    default:
      break;
  }
}

static void TaskUI_DrawCharacter(uint16_t x,
                                 uint16_t y,
                                 char character,
                                 uint8_t scale,
                                 uint16_t color)
{
  uint8_t glyph[5];
  uint8_t column;
  uint8_t row;

  TaskUI_GetGlyph(character, glyph);
  for (column = 0U; column < 5U; ++column)
  {
    for (row = 0U; row < 7U; ++row)
    {
      if ((glyph[column] & (uint8_t)(1U << row)) != 0U)
      {
        TaskUI_FillRect((uint16_t)(x + ((uint16_t)column * scale)),
                        (uint16_t)(y + ((uint16_t)row * scale)),
                        scale,
                        scale,
                        color);
      }
    }
  }
}

static void TaskUI_DrawText(uint16_t x,
                            uint16_t y,
                            const char *text,
                            uint8_t scale,
                            uint16_t color)
{
  while ((text != NULL) && (*text != '\0'))
  {
    TaskUI_DrawCharacter(x, y, *text, scale, color);
    x = (uint16_t)(x + (6U * scale));
    ++text;
  }
}

static HAL_StatusTypeDef TaskUI_Render(void)
{
  uint8_t task;

  TaskUI_FillRect(0U, 0U, ST7789_WIDTH, ST7789_HEIGHT, TASK_UI_BACKGROUND);
  TaskUI_FillRect(0U, 0U, ST7789_WIDTH, 36U, TASK_UI_HEADER);
  TaskUI_DrawText(20U, 11U, "SELECT TASK", 2U, ST7789_COLOR_WHITE);

  for (task = 0U; task < TASK_UI_TASK_COUNT; ++task)
  {
    uint16_t y = (uint16_t)(43U + ((uint16_t)task * 37U));
    uint16_t item_color =
        (task == selected_index) ? TASK_UI_SELECTED : TASK_UI_ITEM;
    uint16_t text_color =
        (task == selected_index) ? ST7789_COLOR_BLACK : ST7789_COLOR_WHITE;
    char label[] = "TASK 1";

    label[5] = (char)('1' + task);
    TaskUI_FillRect(8U, y, 156U, 32U, item_color);
    TaskUI_DrawBorder(8U, y, 156U, 32U, TASK_UI_BORDER);
    TaskUI_DrawText(32U, (uint16_t)(y + 6U), label, 3U, text_color);
  }

  TaskUI_DrawText(15U, 274U, "B1:UP  B2:DOWN", 1U, TASK_UI_MUTED);
  TaskUI_DrawText(61U, 294U, "B3:RUN", 1U, ST7789_COLOR_WHITE);

  if (ST7789_DrawRGB565_DMA(0U,
                            0U,
                            ST7789_WIDTH,
                            ST7789_HEIGHT,
                            framebuffer) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return ST7789_Wait(TASK_UI_RENDER_TIMEOUT_MS);
}

static uint8_t TaskUI_ReadPressed(const TaskUI_ButtonTypeDef *button)
{
  return (uint8_t)(HAL_GPIO_ReadPin(button->port, button->pin) ==
                   GPIO_PIN_RESET);
}

static uint8_t TaskUI_UpdateButton(TaskUI_ButtonTypeDef *button,
                                   uint32_t now)
{
  uint8_t pressed = TaskUI_ReadPressed(button);

  if (pressed != button->candidate_pressed)
  {
    button->candidate_pressed = pressed;
    button->candidate_since = now;
  }
  else if ((pressed != button->stable_pressed) &&
           ((uint32_t)(now - button->candidate_since) >=
            TASK_UI_DEBOUNCE_MS))
  {
    button->stable_pressed = pressed;
    return pressed;
  }
  return 0U;
}

HAL_StatusTypeDef TaskUI_Init(void)
{
  uint8_t index;
  uint32_t now = HAL_GetTick();

  selected_index = 0U;
  for (index = 0U; index < 3U; ++index)
  {
    uint8_t pressed = TaskUI_ReadPressed(&buttons[index]);
    buttons[index].stable_pressed = pressed;
    buttons[index].candidate_pressed = pressed;
    buttons[index].candidate_since = now;
  }
  ui_initialized = 1U;
  return TaskUI_Render();
}

uint8_t TaskUI_Process(uint8_t *selected_task)
{
  uint32_t now;
  uint8_t up_pressed;
  uint8_t down_pressed;
  uint8_t run_pressed;

  if ((ui_initialized == 0U) || (selected_task == NULL))
  {
    return 0U;
  }

  now = HAL_GetTick();
  up_pressed = TaskUI_UpdateButton(&buttons[0], now);
  down_pressed = TaskUI_UpdateButton(&buttons[1], now);
  run_pressed = TaskUI_UpdateButton(&buttons[2], now);

  if (up_pressed != 0U)
  {
    selected_index = (selected_index == 0U) ?
                     (TASK_UI_TASK_COUNT - 1U) :
                     (uint8_t)(selected_index - 1U);
    (void)TaskUI_Render();
  }
  else if (down_pressed != 0U)
  {
    selected_index = (uint8_t)((selected_index + 1U) %
                               TASK_UI_TASK_COUNT);
    (void)TaskUI_Render();
  }

  if (run_pressed != 0U)
  {
    *selected_task = selected_index;
    return 1U;
  }
  return 0U;
}

uint8_t TaskUI_GetSelection(void)
{
  return selected_index;
}
