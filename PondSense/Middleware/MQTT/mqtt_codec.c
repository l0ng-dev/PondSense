/**
 * @file    mqtt_codec.c
 * @brief   MQTT 3.1.1 报文编解码实现。
 *
 * @details
 * 构造 CONNECT、SUBSCRIBE、PUBLISH、PING 报文，并解析剩余长度和响应字段。
 */

#include "mqtt_codec.h"
#include <string.h>

static uint16_t BSP_MQTT_WriteString(uint8_t *buffer,
                                     uint16_t offset,
                                     uint16_t capacity,
                                     const char *text)
{
  size_t length = strlen(text);

  if ((length > UINT16_MAX) ||
      ((uint32_t)offset + 2UL + (uint32_t)length > capacity))
  {
    return 0U;
  }
  buffer[offset++] = (uint8_t)(length >> 8);
  buffer[offset++] = (uint8_t)length;
  (void)memcpy(&buffer[offset], text, length);
  return (uint16_t)(offset + (uint16_t)length);
}


static uint8_t BSP_MQTT_EncodeRemainingLength(uint32_t length,
                                               uint8_t *encoded)
{
  /* MQTT 剩余长度采用最多四字节的 128 进制变长编码。 */
  uint8_t count = 0U;

  do
  {
    uint8_t digit = (uint8_t)(length % 128UL);
    length /= 128UL;
    if (length > 0UL)
    {
      digit |= 0x80U;
    }
    encoded[count++] = digit;
  } while ((length > 0UL) && (count < 4U));

  return count;
}


uint16_t BSP_MQTT_BuildConnect(BSP_MQTT_HandleTypeDef *mqtt)
{
  uint8_t remaining[4];
  uint8_t remaining_size;
  uint32_t remaining_length = 10UL + 2UL + strlen(mqtt->key);
  uint16_t offset = 0U;

  remaining_size = BSP_MQTT_EncodeRemainingLength(remaining_length, remaining);
  if ((1UL + remaining_size + remaining_length) > sizeof(mqtt->packet))
  {
    return 0U;
  }

  mqtt->packet[offset++] = 0x10U;
  (void)memcpy(&mqtt->packet[offset], remaining, remaining_size);
  offset += remaining_size;
  mqtt->packet[offset++] = 0x00U;
  mqtt->packet[offset++] = 0x04U;
  mqtt->packet[offset++] = 'M';
  mqtt->packet[offset++] = 'Q';
  mqtt->packet[offset++] = 'T';
  mqtt->packet[offset++] = 'T';
  mqtt->packet[offset++] = 0x04U;
  mqtt->packet[offset++] = 0x02U;
  mqtt->packet[offset++] = 0x00U;
  mqtt->packet[offset++] = 60U;
  return BSP_MQTT_WriteString(mqtt->packet,
                              offset,
                              sizeof(mqtt->packet),
                              mqtt->key);
}


uint16_t BSP_MQTT_BuildSubscribe(BSP_MQTT_HandleTypeDef *mqtt)
{
  uint8_t remaining[4];
  uint8_t remaining_size;
  uint32_t remaining_length = 2UL + 2UL + strlen(mqtt->base_topic) + 1UL;
  uint16_t offset = 0U;

  remaining_size = BSP_MQTT_EncodeRemainingLength(remaining_length, remaining);
  if ((1UL + remaining_size + remaining_length) > sizeof(mqtt->packet))
  {
    return 0U;
  }
  mqtt->packet_id++;
  if (mqtt->packet_id == 0U)
  {
    mqtt->packet_id = 1U;
  }

  mqtt->packet[offset++] = 0x82U;
  (void)memcpy(&mqtt->packet[offset], remaining, remaining_size);
  offset += remaining_size;
  mqtt->packet[offset++] = (uint8_t)(mqtt->packet_id >> 8);
  mqtt->packet[offset++] = (uint8_t)mqtt->packet_id;
  offset = BSP_MQTT_WriteString(mqtt->packet,
                                offset,
                                sizeof(mqtt->packet),
                                mqtt->base_topic);
  if (offset == 0U)
  {
    return 0U;
  }
  mqtt->packet[offset++] = 0x00U;
  return offset;
}


uint16_t BSP_MQTT_BuildPublish(BSP_MQTT_HandleTypeDef *mqtt)
{
  uint8_t remaining[4];
  uint8_t remaining_size;
  size_t message_length = strlen(mqtt->pending_message);
  uint32_t remaining_length = 2UL + strlen(mqtt->pending_topic) +
                              message_length;
  uint16_t offset = 0U;

  remaining_size = BSP_MQTT_EncodeRemainingLength(remaining_length, remaining);
  if ((1UL + remaining_size + remaining_length) > sizeof(mqtt->packet))
  {
    return 0U;
  }
  mqtt->packet[offset++] = 0x30U;
  (void)memcpy(&mqtt->packet[offset], remaining, remaining_size);
  offset += remaining_size;
  offset = BSP_MQTT_WriteString(mqtt->packet,
                                offset,
                                sizeof(mqtt->packet),
                                mqtt->pending_topic);
  if (offset == 0U)
  {
    return 0U;
  }
  (void)memcpy(&mqtt->packet[offset], mqtt->pending_message, message_length);
  return (uint16_t)(offset + message_length);
}


uint8_t BSP_MQTT_DecodeRemainingLength(const uint8_t *packet,
                                               uint16_t length,
                                               uint32_t *remaining_length,
                                               uint8_t *header_length)
{
  /* 高位为续传标志；只有读到最后一字节后才能确定完整报文长度。 */
  uint32_t multiplier = 1UL;
  uint32_t value = 0UL;
  uint8_t index = 1U;

  while ((index < length) && (index <= 4U))
  {
    uint8_t digit = packet[index++];
    value += (uint32_t)(digit & 0x7FU) * multiplier;
    if ((digit & 0x80U) == 0U)
    {
      *remaining_length = value;
      *header_length = index;
      return 1U;
    }
    multiplier *= 128UL;
  }
  return 0U;
}
