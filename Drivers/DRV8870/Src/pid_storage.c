#include "pid_storage.h"

#include <stddef.h>
#include <string.h>

#define PID_STORAGE_MAGIC          0x50494443UL
#define PID_STORAGE_VERSION        1U
#define PID_STORAGE_SLOT_COUNT     2U
#define PID_STORAGE_SLOT_SPACING   64U

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t record_size;
  uint32_t sequence;
  LineFollower_PIDConfigTypeDef config;
  uint32_t crc32;
} PIDStorage_RecordTypeDef;

_Static_assert(sizeof(PIDStorage_RecordTypeDef) == 40U,
               "PID EEPROM record layout changed");

static AT24CS32_HandleTypeDef pid_eeprom;
static uint8_t storage_initialized;

static uint32_t PIDStorage_CRC32(const void *data, size_t length)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t crc = 0xFFFFFFFFUL;
  size_t index;

  for (index = 0U; index < length; ++index)
  {
    uint8_t bit;
    crc ^= bytes[index];
    for (bit = 0U; bit < 8U; ++bit)
    {
      uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

static uint16_t PIDStorage_SlotAddress(uint8_t slot)
{
  return (uint16_t)((uint16_t)slot * PID_STORAGE_SLOT_SPACING);
}

static uint8_t PIDStorage_IsRecordValid(
    const PIDStorage_RecordTypeDef *record)
{
  uint32_t expected_crc;

  if ((record->magic != PID_STORAGE_MAGIC) ||
      (record->version != PID_STORAGE_VERSION) ||
      (record->record_size != sizeof(PIDStorage_RecordTypeDef)))
  {
    return 0U;
  }

  expected_crc = PIDStorage_CRC32(record,
                                 offsetof(PIDStorage_RecordTypeDef, crc32));
  return (uint8_t)(expected_crc == record->crc32);
}

static PIDStorage_StatusTypeDef PIDStorage_ReadSlot(
    uint8_t slot,
    PIDStorage_RecordTypeDef *record)
{
  if (AT24CS32_Read(&pid_eeprom,
                    PIDStorage_SlotAddress(slot),
                    record,
                    sizeof(*record)) != AT24CS32_OK)
  {
    return PID_STORAGE_ERROR_EEPROM;
  }
  return PID_STORAGE_OK;
}

static uint8_t PIDStorage_IsSequenceNewer(uint32_t candidate,
                                         uint32_t reference)
{
  return (uint8_t)((int32_t)(candidate - reference) > 0);
}

PIDStorage_StatusTypeDef PIDStorage_Init(I2C_HandleTypeDef *i2c)
{
  storage_initialized = 0U;
  if (AT24CS32_Init(&pid_eeprom, i2c, 0U) != AT24CS32_OK)
  {
    return PID_STORAGE_ERROR_PARAMETER;
  }
  if (AT24CS32_IsReady(&pid_eeprom) != AT24CS32_OK)
  {
    return PID_STORAGE_ERROR_EEPROM;
  }
  storage_initialized = 1U;
  return PID_STORAGE_OK;
}

PIDStorage_StatusTypeDef PIDStorage_Load(
    LineFollower_PIDConfigTypeDef *config)
{
  PIDStorage_RecordTypeDef records[PID_STORAGE_SLOT_COUNT];
  uint8_t valid[PID_STORAGE_SLOT_COUNT] = {0U};
  uint8_t slot;
  uint8_t selected;

  if ((storage_initialized == 0U) || (config == NULL))
  {
    return PID_STORAGE_ERROR_PARAMETER;
  }

  for (slot = 0U; slot < PID_STORAGE_SLOT_COUNT; ++slot)
  {
    if (PIDStorage_ReadSlot(slot, &records[slot]) != PID_STORAGE_OK)
    {
      return PID_STORAGE_ERROR_EEPROM;
    }
    valid[slot] = PIDStorage_IsRecordValid(&records[slot]);
  }

  if ((valid[0] == 0U) && (valid[1] == 0U))
  {
    return PID_STORAGE_NOT_FOUND;
  }
  selected = (valid[0] != 0U) ? 0U : 1U;
  if ((valid[0] != 0U) && (valid[1] != 0U) &&
      (PIDStorage_IsSequenceNewer(records[1].sequence,
                                  records[0].sequence) != 0U))
  {
    selected = 1U;
  }

  *config = records[selected].config;
  return PID_STORAGE_OK;
}

PIDStorage_StatusTypeDef PIDStorage_Save(
    const LineFollower_PIDConfigTypeDef *config)
{
  PIDStorage_RecordTypeDef records[PID_STORAGE_SLOT_COUNT];
  PIDStorage_RecordTypeDef new_record;
  PIDStorage_RecordTypeDef verify_record;
  uint8_t valid[PID_STORAGE_SLOT_COUNT] = {0U};
  uint8_t newest_slot = 0U;
  uint8_t target_slot;
  uint8_t slot;

  if ((storage_initialized == 0U) || (config == NULL))
  {
    return PID_STORAGE_ERROR_PARAMETER;
  }

  for (slot = 0U; slot < PID_STORAGE_SLOT_COUNT; ++slot)
  {
    if (PIDStorage_ReadSlot(slot, &records[slot]) != PID_STORAGE_OK)
    {
      return PID_STORAGE_ERROR_EEPROM;
    }
    valid[slot] = PIDStorage_IsRecordValid(&records[slot]);
  }

  if ((valid[1] != 0U) &&
      ((valid[0] == 0U) ||
       (PIDStorage_IsSequenceNewer(records[1].sequence,
                                   records[0].sequence) != 0U)))
  {
    newest_slot = 1U;
  }
  target_slot = (uint8_t)(newest_slot ^ 1U);

  memset(&new_record, 0, sizeof(new_record));
  new_record.magic = PID_STORAGE_MAGIC;
  new_record.version = PID_STORAGE_VERSION;
  new_record.record_size = sizeof(new_record);
  new_record.sequence = ((valid[0] != 0U) || (valid[1] != 0U)) ?
                        (records[newest_slot].sequence + 1U) : 1U;
  new_record.config = *config;
  new_record.crc32 = PIDStorage_CRC32(
      &new_record, offsetof(PIDStorage_RecordTypeDef, crc32));

  if (AT24CS32_Write(&pid_eeprom,
                     PIDStorage_SlotAddress(target_slot),
                     &new_record,
                     sizeof(new_record)) != AT24CS32_OK)
  {
    return PID_STORAGE_ERROR_EEPROM;
  }
  if ((PIDStorage_ReadSlot(target_slot, &verify_record) != PID_STORAGE_OK) ||
      (memcmp(&new_record, &verify_record, sizeof(new_record)) != 0))
  {
    return PID_STORAGE_ERROR_VERIFY;
  }
  return PID_STORAGE_OK;
}
