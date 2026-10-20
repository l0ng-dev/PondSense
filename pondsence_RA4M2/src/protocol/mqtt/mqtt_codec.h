#ifndef PONDSENSE_MQTT_CODEC_H
#define PONDSENSE_MQTT_CODEC_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    PONDSENSE_MQTT_FRAME_INCOMPLETE = 0,
    PONDSENSE_MQTT_FRAME_READY,
    /* 报文长度已解析，但超过调用方缓冲区；长度输出参数仍有效。 */
    PONDSENSE_MQTT_FRAME_OVERSIZE,
    PONDSENSE_MQTT_FRAME_INVALID
} pondsense_mqtt_frame_status_t;

uint16_t PondSense_Mqtt_BuildConnect(uint8_t * buffer, size_t capacity, char const * client_id);
uint16_t PondSense_Mqtt_BuildSubscribe(uint8_t * buffer, size_t capacity,
                                      uint16_t packet_id, char const * topic);
uint16_t PondSense_Mqtt_BuildPublish(uint8_t * buffer, size_t capacity,
                                    char const * topic, char const * payload);
uint16_t PondSense_Mqtt_BuildPing(uint8_t * buffer, size_t capacity);

pondsense_mqtt_frame_status_t PondSense_Mqtt_PeekFrame(uint8_t const * data,
                                                       size_t length,
                                                       size_t capacity,
                                                       uint32_t * frame_length,
                                                       uint16_t * header_length,
                                                       uint32_t * remaining);

#endif
