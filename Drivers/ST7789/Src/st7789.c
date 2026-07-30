#include "st7789.h"

#include "main.h"

#include <stddef.h>

#define ST7789_CMD_SWRESET  0x01U
#define ST7789_CMD_SLPOUT   0x11U
#define ST7789_CMD_NORON    0x13U
#define ST7789_CMD_INVON    0x21U
#define ST7789_CMD_DISPON   0x29U
#define ST7789_CMD_CASET    0x2AU
#define ST7789_CMD_RASET    0x2BU
#define ST7789_CMD_RAMWR    0x2CU
#define ST7789_CMD_MADCTL   0x36U
#define ST7789_CMD_COLMOD   0x3AU

#define ST7789_X_OFFSET         34U
#define ST7789_SPI_TIMEOUT_MS   100U
#define ST7789_CACHE_LINE_SIZE  32U
#define ST7789_ROW_BYTES        (ST7789_WIDTH * 2U)

typedef enum
{
  ST7789_TRANSFER_NONE = 0,
  ST7789_TRANSFER_FILL,
  ST7789_TRANSFER_PIXELS
} ST7789_TransferTypeDef;

static SPI_HandleTypeDef *display_spi;
static volatile uint8_t display_initialized;
static volatile uint8_t transfer_busy;
static volatile HAL_StatusTypeDef transfer_status = HAL_OK;
static ST7789_TransferTypeDef transfer_type;
static const uint16_t *transfer_pixels;
static uint16_t transfer_color;
static uint16_t transfer_width;
static uint16_t transfer_height;
static uint16_t transfer_row;

/*
 * DMA1 cannot access DTCM. The linker places this aligned staging buffer in
 * AXI SRAM (RAM_D1), which is reachable by both the CPU and DMA1.
 */
__attribute__((section(".dma_buffer"), aligned(ST7789_CACHE_LINE_SIZE)))
static uint8_t dma_row_buffer[ST7789_ROW_BYTES];

static void ST7789_Select(void)
{
  HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);
}

static void ST7789_Unselect(void)
{
  HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef ST7789_Write(const uint8_t *data,
                                     uint16_t length,
                                     GPIO_PinState dc_state)
{
  HAL_StatusTypeDef status;

  if ((data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  HAL_GPIO_WritePin(LCD_D_C_GPIO_Port, LCD_D_C_Pin, dc_state);
  ST7789_Select();
  status = HAL_SPI_Transmit(display_spi,
                            data,
                            length,
                            ST7789_SPI_TIMEOUT_MS);
  ST7789_Unselect();
  return status;
}

static HAL_StatusTypeDef ST7789_WriteCommand(uint8_t command)
{
  return ST7789_Write(&command, 1U, GPIO_PIN_RESET);
}

static HAL_StatusTypeDef ST7789_WriteData(const uint8_t *data, uint16_t length)
{
  return ST7789_Write(data, length, GPIO_PIN_SET);
}

static HAL_StatusTypeDef ST7789_WriteCommandData(uint8_t command,
                                                const uint8_t *data,
                                                uint16_t length)
{
  if (ST7789_WriteCommand(command) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if ((length != 0U) && (ST7789_WriteData(data, length) != HAL_OK))
  {
    return HAL_ERROR;
  }
  return HAL_OK;
}

static void ST7789_Reset(void)
{
  ST7789_Unselect();
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(10U);
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(20U);
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(120U);
}

static HAL_StatusTypeDef ST7789_SetWindow(uint16_t x,
                                         uint16_t y,
                                         uint16_t width,
                                         uint16_t height)
{
  uint16_t x_start = (uint16_t)(x + ST7789_X_OFFSET);
  uint16_t x_end = (uint16_t)(x_start + width - 1U);
  uint16_t y_end = (uint16_t)(y + height - 1U);
  uint8_t address[4];

  address[0] = (uint8_t)(x_start >> 8U);
  address[1] = (uint8_t)x_start;
  address[2] = (uint8_t)(x_end >> 8U);
  address[3] = (uint8_t)x_end;
  if (ST7789_WriteCommandData(ST7789_CMD_CASET,
                              address,
                              sizeof(address)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  address[0] = (uint8_t)(y >> 8U);
  address[1] = (uint8_t)y;
  address[2] = (uint8_t)(y_end >> 8U);
  address[3] = (uint8_t)y_end;
  if (ST7789_WriteCommandData(ST7789_CMD_RASET,
                              address,
                              sizeof(address)) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return ST7789_WriteCommand(ST7789_CMD_RAMWR);
}

static void ST7789_PrepareRow(void)
{
  uint16_t column;

  for (column = 0U; column < transfer_width; ++column)
  {
    uint16_t color;
    if (transfer_type == ST7789_TRANSFER_FILL)
    {
      color = transfer_color;
    }
    else
    {
      color = transfer_pixels[((uint32_t)transfer_row * transfer_width) +
                              column];
    }
    dma_row_buffer[(uint32_t)column * 2U] = (uint8_t)(color >> 8U);
    dma_row_buffer[((uint32_t)column * 2U) + 1U] = (uint8_t)color;
  }

  if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
  {
    uint32_t clean_size =
        (((uint32_t)transfer_width * 2U) + ST7789_CACHE_LINE_SIZE - 1U) &
        ~(ST7789_CACHE_LINE_SIZE - 1U);
    SCB_CleanDCache_by_Addr((uint32_t *)dma_row_buffer,
                            (int32_t)clean_size);
  }
}

static void ST7789_EndTransfer(HAL_StatusTypeDef status)
{
  ST7789_Unselect();
  transfer_status = status;
  transfer_type = ST7789_TRANSFER_NONE;
  transfer_busy = 0U;
}

static HAL_StatusTypeDef ST7789_StartTransfer(uint16_t x,
                                             uint16_t y,
                                             uint16_t width,
                                             uint16_t height,
                                             ST7789_TransferTypeDef type,
                                             const uint16_t *pixels,
                                             uint16_t color)
{
  if ((display_initialized == 0U) || (transfer_busy != 0U))
  {
    return HAL_BUSY;
  }
  if ((width == 0U) || (height == 0U) ||
      (((uint32_t)x + width) > ST7789_WIDTH) ||
      (((uint32_t)y + height) > ST7789_HEIGHT) ||
      ((type == ST7789_TRANSFER_PIXELS) && (pixels == NULL)))
  {
    return HAL_ERROR;
  }
  if (ST7789_SetWindow(x, y, width, height) != HAL_OK)
  {
    return HAL_ERROR;
  }

  transfer_type = type;
  transfer_pixels = pixels;
  transfer_color = color;
  transfer_width = width;
  transfer_height = height;
  transfer_row = 0U;
  transfer_status = HAL_OK;
  transfer_busy = 1U;

  ST7789_PrepareRow();
  HAL_GPIO_WritePin(LCD_D_C_GPIO_Port, LCD_D_C_Pin, GPIO_PIN_SET);
  ST7789_Select();
  if (HAL_SPI_Transmit_DMA(display_spi,
                           dma_row_buffer,
                           (uint16_t)(width * 2U)) != HAL_OK)
  {
    ST7789_EndTransfer(HAL_ERROR);
    return HAL_ERROR;
  }
  return HAL_OK;
}

HAL_StatusTypeDef ST7789_Init(SPI_HandleTypeDef *spi)
{
  static const uint8_t porch[] = {0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U};
  static const uint8_t power_control[] = {0xA4U, 0xA1U};
  static const uint8_t positive_gamma[] =
  {
    0xF0U, 0x00U, 0x04U, 0x04U, 0x04U, 0x05U, 0x29U,
    0x33U, 0x3EU, 0x38U, 0x12U, 0x12U, 0x28U, 0x30U
  };
  static const uint8_t negative_gamma[] =
  {
    0xF0U, 0x07U, 0x0AU, 0x0DU, 0x0BU, 0x07U, 0x28U,
    0x33U, 0x3EU, 0x36U, 0x14U, 0x14U, 0x29U, 0x32U
  };
  uint8_t value;

  if ((spi == NULL) || (spi->hdmatx == NULL))
  {
    return HAL_ERROR;
  }

  display_spi = spi;
  display_initialized = 0U;
  transfer_busy = 0U;
  transfer_type = ST7789_TRANSFER_NONE;
  ST7789_Reset();

  if (ST7789_WriteCommand(ST7789_CMD_SWRESET) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(150U);
  if (ST7789_WriteCommand(ST7789_CMD_SLPOUT) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(120U);

  value = 0x00U;
  if (ST7789_WriteCommandData(ST7789_CMD_MADCTL, &value, 1U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  value = 0x05U;
  if (ST7789_WriteCommandData(ST7789_CMD_COLMOD, &value, 1U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (ST7789_WriteCommandData(0xB2U, porch, sizeof(porch)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  value = 0x35U;
  if ((ST7789_WriteCommandData(0xB7U, &value, 1U) != HAL_OK) ||
      (ST7789_WriteCommandData(0xBBU, &value, 1U) != HAL_OK))
  {
    return HAL_ERROR;
  }
  value = 0x2CU;
  if (ST7789_WriteCommandData(0xC0U, &value, 1U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  value = 0x01U;
  if (ST7789_WriteCommandData(0xC2U, &value, 1U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  value = 0x13U;
  if (ST7789_WriteCommandData(0xC3U, &value, 1U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  value = 0x20U;
  if (ST7789_WriteCommandData(0xC4U, &value, 1U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  value = 0x0FU;
  if (ST7789_WriteCommandData(0xC6U, &value, 1U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if (ST7789_WriteCommandData(0xD0U,
                              power_control,
                              sizeof(power_control)) != HAL_OK)
  {
    return HAL_ERROR;
  }
  value = 0xA1U;
  if (ST7789_WriteCommandData(0xD6U, &value, 1U) != HAL_OK)
  {
    return HAL_ERROR;
  }
  if ((ST7789_WriteCommandData(0xE0U,
                               positive_gamma,
                               sizeof(positive_gamma)) != HAL_OK) ||
      (ST7789_WriteCommandData(0xE1U,
                               negative_gamma,
                               sizeof(negative_gamma)) != HAL_OK))
  {
    return HAL_ERROR;
  }
  if ((ST7789_WriteCommand(ST7789_CMD_INVON) != HAL_OK) ||
      (ST7789_WriteCommand(ST7789_CMD_NORON) != HAL_OK))
  {
    return HAL_ERROR;
  }
  HAL_Delay(10U);
  if (ST7789_WriteCommand(ST7789_CMD_DISPON) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(20U);

  display_initialized = 1U;
  return HAL_OK;
}

HAL_StatusTypeDef ST7789_FillScreen_DMA(uint16_t color)
{
  return ST7789_FillRect_DMA(0U,
                            0U,
                            ST7789_WIDTH,
                            ST7789_HEIGHT,
                            color);
}

HAL_StatusTypeDef ST7789_FillRect_DMA(uint16_t x,
                                     uint16_t y,
                                     uint16_t width,
                                     uint16_t height,
                                     uint16_t color)
{
  return ST7789_StartTransfer(x,
                             y,
                             width,
                             height,
                             ST7789_TRANSFER_FILL,
                             NULL,
                             color);
}

HAL_StatusTypeDef ST7789_DrawRGB565_DMA(uint16_t x,
                                       uint16_t y,
                                       uint16_t width,
                                       uint16_t height,
                                       const uint16_t *pixels)
{
  return ST7789_StartTransfer(x,
                             y,
                             width,
                             height,
                             ST7789_TRANSFER_PIXELS,
                             pixels,
                             0U);
}

uint8_t ST7789_IsBusy(void)
{
  return transfer_busy;
}

HAL_StatusTypeDef ST7789_Wait(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  while (transfer_busy != 0U)
  {
    if ((HAL_GetTick() - start) >= timeout_ms)
    {
      (void)HAL_SPI_Abort(display_spi);
      ST7789_EndTransfer(HAL_TIMEOUT);
      break;
    }
  }
  return transfer_status;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *spi)
{
  if ((spi != display_spi) || (transfer_busy == 0U))
  {
    return;
  }

  ++transfer_row;
  if (transfer_row >= transfer_height)
  {
    ST7789_EndTransfer(HAL_OK);
    return;
  }

  ST7789_PrepareRow();
  if (HAL_SPI_Transmit_DMA(display_spi,
                           dma_row_buffer,
                           (uint16_t)(transfer_width * 2U)) != HAL_OK)
  {
    ST7789_EndTransfer(HAL_ERROR);
  }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *spi)
{
  if ((spi == display_spi) && (transfer_busy != 0U))
  {
    ST7789_EndTransfer(HAL_ERROR);
  }
}
