#include "mqtt_codec.h"

#include <string.h>

static uint16_t mqtt_write_string(uint8_t * buffer, size_t capacity,
                                  uint16_t offset, char const * value)
{
    size_t const length = strlen(value);
    if ((length > UINT16_MAX) || ((size_t) offset + 2U + length > capacity))
    {
        return 0U;
    }
    buffer[offset++] = (uint8_t) (length >> 8U);
    buffer[offset++] = (uint8_t) length;
    memcpy(&buffer[offset], value, length);
    return (uint16_t) (offset + length);
}

static uint16_t mqtt_header(uint8_t * buffer, uint8_t packet_type, uint32_t remaining)
{
    uint16_t index = 0U;
    buffer[index++] = packet_type;
    do
    {
        uint8_t digit = (uint8_t) (remaining % 128U);
        remaining /= 128U;
        if (remaining > 0U)
        {
            digit |= 0x80U;
        }
        buffer[index++] = digit;
    } while ((remaining > 0U) && (index < 5U));
    return index;
}

uint16_t PondSense_Mqtt_BuildConnect(uint8_t * buffer, size_t capacity, char const * client_id)
{
    if ((NULL == buffer) || (NULL == client_id) || (capacity > UINT16_MAX))
    {
        return 0U;
    }
    size_t const client_length = strlen(client_id);
    if (client_length > UINT16_MAX)
    {
        return 0U;
    }
    uint32_t const remaining = (uint32_t) (12U + client_length);
    if (remaining + 5U > capacity)
    {
        return 0U;
    }
    uint16_t index = mqtt_header(buffer, 0x10U, remaining);
    static uint8_t const variable_header[] = {
        0x00U, 0x04U, 'M', 'Q', 'T', 'T', 0x04U, 0x02U, 0x00U, 60U
    };
    memcpy(&buffer[index], variable_header, sizeof(variable_header));
    index += (uint16_t) sizeof(variable_header);
    return mqtt_write_string(buffer, capacity, index, client_id);
}

uint16_t PondSense_Mqtt_BuildSubscribe(uint8_t * buffer, size_t capacity,
                                      uint16_t packet_id, char const * topic)
{
    if ((NULL == buffer) || (NULL == topic) || (0U == packet_id) || (capacity > UINT16_MAX))
    {
        return 0U;
    }
    size_t const topic_length = strlen(topic);
    if (topic_length > UINT16_MAX)
    {
        return 0U;
    }
    uint32_t const remaining = (uint32_t) (5U + topic_length);
    if (remaining + 5U > capacity)
    {
        return 0U;
    }
    uint16_t index = mqtt_header(buffer, 0x82U, remaining);
    buffer[index++] = (uint8_t) (packet_id >> 8U);
    buffer[index++] = (uint8_t) packet_id;
    index = mqtt_write_string(buffer, capacity, index, topic);
    if (0U != index)
    {
        buffer[index++] = 0U;
    }
    return index;
}

uint16_t PondSense_Mqtt_BuildPublish(uint8_t * buffer, size_t capacity,
                                    char const * topic, char const * payload)
{
    if ((NULL == buffer) || (NULL == topic) || (NULL == payload) || (capacity > UINT16_MAX))
    {
        return 0U;
    }
    size_t const topic_length = strlen(topic);
    size_t const payload_length = strlen(payload);
    if ((topic_length > UINT16_MAX) || (payload_length > UINT16_MAX))
    {
        return 0U;
    }
    uint32_t const remaining = (uint32_t) (2U + topic_length + payload_length);
    if (remaining + 5U > capacity)
    {
        return 0U;
    }
    uint16_t index = mqtt_header(buffer, 0x30U, remaining);
    index = mqtt_write_string(buffer, capacity, index, topic);
    if (0U == index)
    {
        return 0U;
    }
    memcpy(&buffer[index], payload, payload_length);
    return (uint16_t) (index + payload_length);
}

uint16_t PondSense_Mqtt_BuildPing(uint8_t * buffer, size_t capacity)
{
    if ((NULL == buffer) || (capacity < 2U))
    {
        return 0U;
    }
    buffer[0] = 0xC0U;
    buffer[1] = 0U;
    return 2U;
}

pondsense_mqtt_frame_status_t PondSense_Mqtt_PeekFrame(uint8_t const * data,
                                                       size_t length,
                                                       size_t capacity,
                                                       uint32_t * frame_length,
                                                       uint16_t * header_length,
                                                       uint32_t * remaining)
{
    if ((NULL == data) || (NULL == frame_length) || (NULL == header_length) ||
        (NULL == remaining) || (length > capacity) || (capacity > UINT16_MAX))
    {
        return PONDSENSE_MQTT_FRAME_INVALID;
    }
    if (length < 2U)
    {
        return PONDSENSE_MQTT_FRAME_INCOMPLETE;
    }
    uint32_t body_length = 0U;
    uint32_t multiplier = 1U;
    uint16_t header = 1U;
    uint8_t digit;
    do
    {
        if (header >= length)
        {
            return PONDSENSE_MQTT_FRAME_INCOMPLETE;
        }
        if (header > 4U)
        {
            return PONDSENSE_MQTT_FRAME_INVALID;
        }
        digit = data[header++];
        body_length += (uint32_t) (digit & 0x7FU) * multiplier;
        multiplier *= 128U;
    } while (0U != (digit & 0x80U));
    uint32_t const total = header + body_length;
    *frame_length = total;
    *header_length = header;
    *remaining = body_length;
    if (total > capacity)
    {
        return PONDSENSE_MQTT_FRAME_OVERSIZE;
    }
    if (total > length)
    {
        return PONDSENSE_MQTT_FRAME_INCOMPLETE;
    }
    return PONDSENSE_MQTT_FRAME_READY;
}
