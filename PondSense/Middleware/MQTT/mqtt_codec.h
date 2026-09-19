/**
 * @file    mqtt_codec.h
 * @brief   MQTT 3.1.1 报文编解码接口。
 *
 * @details
 * 为 MQTT 客户端状态机提供 CONNECT、SUBSCRIBE、PUBLISH、PING 和响应解析能力。
 */

#ifndef MQTT_CODEC_H
#define MQTT_CODEC_H

#include "mqtt_client.h"

uint16_t BSP_MQTT_BuildConnect(BSP_MQTT_HandleTypeDef *mqtt);
uint16_t BSP_MQTT_BuildSubscribe(BSP_MQTT_HandleTypeDef *mqtt);
uint16_t BSP_MQTT_BuildPublish(BSP_MQTT_HandleTypeDef *mqtt);
uint8_t BSP_MQTT_DecodeRemainingLength(const uint8_t *packet,
                                       uint16_t length,
                                       uint32_t *remaining_length,
                                       uint8_t *header_length);

#endif /* MQTT_CODEC_H */
