/**
 * @file    mqtt_client.c
 * @brief   非阻塞 MQTT 3.1.1 客户端状态机。
 *
 * @details
 * 基于 ESP-01S TCP 通道推进连接、订阅、发布、心跳和退避重连，
 * 不在应用主循环中等待网络响应。
 */

#include "mqtt_client.h"
#include "mqtt_codec.h"
#include <stdio.h>
#include <string.h>

#define BSP_MQTT_RESPONSE_TIMEOUT_MS 5000U
#define BSP_MQTT_KEEPALIVE_MS 30000U
#define BSP_MQTT_BACKOFF_MS 3000U

static uint8_t BSP_MQTT_TimeReached(uint32_t now_ms, uint32_t deadline_ms)
{
  /* 有符号差值使短期超时判断兼容 32 位 tick 回绕。 */
  return ((int32_t)(now_ms - deadline_ms) >= 0) ? 1U : 0U;
}


static void BSP_MQTT_EnterBackoff(BSP_MQTT_HandleTypeDef *mqtt,
                                  BSP_MQTT_ErrorTypeDef error,
                                  uint32_t now_ms)
{
  if ((mqtt->state == BSP_MQTT_STATE_WAIT_PUBLISH_RESULT) &&
      (mqtt->publish_pending != 0U))
  {
    mqtt->publish_failure_count++;
  }
  mqtt->reconnect_count++;
  mqtt->last_error = error;
  mqtt->state = BSP_MQTT_STATE_BACKOFF;
  mqtt->next_action_ms = now_ms + BSP_MQTT_BACKOFF_MS;
  (void)BSP_WiFi_TcpClose(mqtt->wifi);
}


static void BSP_MQTT_HandlePacket(BSP_MQTT_HandleTypeDef *mqtt,
                                  uint32_t now_ms)
{
  uint32_t remaining_length;
  uint8_t header_length;
  uint8_t packet_type = mqtt->packet[0] >> 4;

  if (BSP_MQTT_DecodeRemainingLength(mqtt->packet,
                                     mqtt->packet_length,
                                     &remaining_length,
                                     &header_length) == 0U)
  {
    return;
  }
  mqtt->rx_packet_count++;

  /* CONNECT 成功后立即进入订阅阶段。 */
  if ((packet_type == 2U) && (remaining_length == 2UL) &&
      (mqtt->state == BSP_MQTT_STATE_WAIT_CONNACK))
  {
    mqtt->connack_code = mqtt->packet[header_length + 1U];
    if (mqtt->connack_code == 0U)
    {
      mqtt->connect_success_count++;
      mqtt->last_error = BSP_MQTT_ERROR_NONE;
      mqtt->state = BSP_MQTT_STATE_SEND_SUBSCRIBE;
    }
    else
    {
      BSP_MQTT_EnterBackoff(mqtt, BSP_MQTT_ERROR_CONNACK, now_ms);
    }
  }
  /* SUBACK 返回 0x80 表示订阅失败，其余返回码均表示已授权的 QoS。 */
  else if ((packet_type == 9U) && (remaining_length >= 3UL) &&
           (mqtt->state == BSP_MQTT_STATE_WAIT_SUBACK))
  {
    mqtt->suback_code = mqtt->packet[header_length + 2U];
    if (mqtt->suback_code != 0x80U)
    {
      mqtt->subscribe_success_count++;
      mqtt->last_error = BSP_MQTT_ERROR_NONE;
      mqtt->state = BSP_MQTT_STATE_ONLINE;
      mqtt->last_tx_ms = now_ms;
    }
    else
    {
      BSP_MQTT_EnterBackoff(mqtt, BSP_MQTT_ERROR_SUBACK, now_ms);
    }
  }
  else if ((packet_type == 13U) &&
           (mqtt->state == BSP_MQTT_STATE_WAIT_PINGRESP))
  {
    mqtt->state = BSP_MQTT_STATE_ONLINE;
    mqtt->last_error = BSP_MQTT_ERROR_NONE;
  }
  /* 当前业务只消费 QoS 0 下行正文；主题字段用于定位正文起点。 */
  else if ((packet_type == 3U) && (remaining_length >= 2UL))
  {
    uint16_t topic_length =
        (uint16_t)(((uint16_t)mqtt->packet[header_length] << 8) |
                   mqtt->packet[header_length + 1U]);
    uint32_t payload_offset =
        (uint32_t)header_length + 2UL + (uint32_t)topic_length;
    uint32_t total_length = (uint32_t)header_length + remaining_length;
    uint16_t payload_length;

    if ((mqtt->packet[0] & 0x06U) != 0U)
    {
      payload_offset += 2UL;
    }
    if (payload_offset <= total_length)
    {
      payload_length = (uint16_t)(total_length - payload_offset);
      if (payload_length >= sizeof(mqtt->last_message))
      {
        payload_length = sizeof(mqtt->last_message) - 1U;
      }
      (void)memcpy(mqtt->last_message,
                   &mqtt->packet[(uint16_t)payload_offset],
                   payload_length);
      mqtt->last_message[payload_length] = '\0';
      mqtt->message_pending = 1U;
    }
    else
    {
      mqtt->last_error = BSP_MQTT_ERROR_PROTOCOL;
    }
  }
}


static void BSP_MQTT_ConsumeByte(BSP_MQTT_HandleTypeDef *mqtt,
                                 uint8_t value,
                                 uint32_t now_ms)
{
  uint32_t remaining_length;
  uint8_t header_length;

  if (mqtt->packet_length >= sizeof(mqtt->packet))
  {
    mqtt->packet_length = 0U;
    mqtt->last_error = BSP_MQTT_ERROR_PACKET_TOO_LARGE;
  }
  mqtt->packet[mqtt->packet_length++] = value;

  /* 剩余长度字段完整后即可计算报文边界，无需等待 TCP 分包对齐。 */
  if (BSP_MQTT_DecodeRemainingLength(mqtt->packet,
                                     mqtt->packet_length,
                                     &remaining_length,
                                     &header_length) != 0U)
  {
    uint32_t total_length = (uint32_t)header_length + remaining_length;
    if (total_length > sizeof(mqtt->packet))
    {
      mqtt->packet_length = 0U;
      mqtt->last_error = BSP_MQTT_ERROR_PACKET_TOO_LARGE;
    }
    else if (mqtt->packet_length == total_length)
    {
      BSP_MQTT_HandlePacket(mqtt, now_ms);
      mqtt->packet_length = 0U;
    }
  }
}


HAL_StatusTypeDef BSP_MQTT_Init(BSP_MQTT_HandleTypeDef *mqtt,
                                BSP_WiFi_HandleTypeDef *wifi,
                                const char *broker_host,
                                uint16_t broker_port,
                                const char *base_topic,
                                uint32_t now_ms)
{
  size_t host_length;
  size_t topic_length;

  if ((mqtt == NULL) || (wifi == NULL) || (broker_host == NULL) ||
      (base_topic == NULL) || (broker_port == 0U))
  {
    return HAL_ERROR;
  }
  host_length = strlen(broker_host);
  topic_length = strlen(base_topic);
  if ((host_length == 0U) ||
      (host_length > BSP_WIFI_TCP_HOST_MAX_LENGTH) ||
      (topic_length == 0U) ||
      (topic_length > BSP_MQTT_TOPIC_MAX_LENGTH) ||
      ((topic_length + 3U) >= sizeof(mqtt->publish_topic)))
  {
    return HAL_ERROR;
  }

  (void)memset(mqtt, 0, sizeof(*mqtt));
  mqtt->wifi = wifi;
  mqtt->broker_port = broker_port;
  mqtt->state = BSP_MQTT_STATE_WAIT_KEY;
  mqtt->next_action_ms = now_ms;
  (void)snprintf(mqtt->broker_host,
                 sizeof(mqtt->broker_host),
                 "%s",
                 broker_host);
  (void)snprintf(mqtt->base_topic,
                 sizeof(mqtt->base_topic),
                 "%s",
                 base_topic);
  (void)snprintf(mqtt->publish_topic,
                 sizeof(mqtt->publish_topic),
                 "%s/up",
                 base_topic);
  return HAL_OK;
}


HAL_StatusTypeDef BSP_MQTT_SetKey(BSP_MQTT_HandleTypeDef *mqtt,
                                  const char *key)
{
  size_t length;

  if ((mqtt == NULL) || (key == NULL))
  {
    return HAL_ERROR;
  }
  length = strlen(key);
  if ((length == 0U) || (length > BSP_MQTT_KEY_MAX_LENGTH) ||
      (strchr(key, '\r') != NULL) || (strchr(key, '\n') != NULL))
  {
    mqtt->last_error = BSP_MQTT_ERROR_ARGUMENT;
    return HAL_ERROR;
  }

  (void)snprintf(mqtt->key, sizeof(mqtt->key), "%s", key);
  mqtt->key_configured = 1U;
  mqtt->last_error = BSP_MQTT_ERROR_NONE;
  if (mqtt->state == BSP_MQTT_STATE_WAIT_KEY)
  {
    mqtt->state = BSP_MQTT_STATE_WAIT_WIFI;
  }
  return HAL_OK;
}


HAL_StatusTypeDef BSP_MQTT_Connect(BSP_MQTT_HandleTypeDef *mqtt,
                                   uint32_t now_ms)
{
  if (mqtt == NULL)
  {
    return HAL_ERROR;
  }
  if (mqtt->key_configured == 0U)
  {
    mqtt->last_error = BSP_MQTT_ERROR_KEY_REQUIRED;
    mqtt->state = BSP_MQTT_STATE_WAIT_KEY;
    return HAL_ERROR;
  }
  mqtt->connect_requested = 1U;
  mqtt->state = BSP_MQTT_STATE_WAIT_WIFI;
  mqtt->next_action_ms = now_ms;
  return HAL_OK;
}


HAL_StatusTypeDef BSP_MQTT_RequestPublish(BSP_MQTT_HandleTypeDef *mqtt,
                                           const char *message)
{
  size_t topic_length;
  size_t length;

  if ((mqtt == NULL) || (message == NULL))
  {
    return HAL_ERROR;
  }
  topic_length = strlen(mqtt->publish_topic);
  length = strlen(message);
  if ((topic_length == 0U) ||
      (topic_length >= sizeof(mqtt->pending_topic)) ||
      (length == 0U) ||
      (length >= sizeof(mqtt->pending_message)))
  {
    mqtt->last_error = BSP_MQTT_ERROR_PACKET_TOO_LARGE;
    return HAL_ERROR;
  }
  if (mqtt->publish_pending != 0U)
  {
    return HAL_BUSY;
  }
  (void)memcpy(mqtt->pending_topic, mqtt->publish_topic, topic_length + 1U);
  (void)memcpy(mqtt->pending_message, message, length + 1U);
  mqtt->publish_pending = 1U;
  mqtt->publish_request_count++;
  return HAL_OK;
}


void BSP_MQTT_Process(BSP_MQTT_HandleTypeDef *mqtt, uint32_t now_ms)
{
  uint8_t received[32];
  uint16_t count;
  uint16_t index;

  if ((mqtt == NULL) || (mqtt->wifi == NULL))
  {
    return;
  }

  /* 排空本轮已到达的 TCP 数据，逐字节交给 MQTT 报文组装器。 */
  do
  {
    count = BSP_WiFi_TcpRead(mqtt->wifi, received, sizeof(received));
    for (index = 0U; index < count; index++)
    {
      BSP_MQTT_ConsumeByte(mqtt, received[index], now_ms);
    }
  } while (count == sizeof(received));

  /* Wi-Fi 断开时立即释放当前协议报文，等待底层链路自行恢复。 */
  if ((mqtt->state != BSP_MQTT_STATE_WAIT_KEY) &&
      (mqtt->wifi->state != BSP_WIFI_STATE_ONLINE))
  {
    mqtt->state = BSP_MQTT_STATE_WAIT_WIFI;
    mqtt->packet_length = 0U;
    return;
  }

  if (((mqtt->state == BSP_MQTT_STATE_WAIT_CONNACK) ||
       (mqtt->state == BSP_MQTT_STATE_WAIT_SUBACK) ||
       (mqtt->state == BSP_MQTT_STATE_WAIT_PINGRESP)) &&
      (BSP_MQTT_TimeReached(now_ms, mqtt->deadline_ms) != 0U))
  {
    BSP_MQTT_EnterBackoff(mqtt, BSP_MQTT_ERROR_TIMEOUT, now_ms);
    return;
  }

  if (((mqtt->state >= BSP_MQTT_STATE_SEND_CONNECT) &&
       (mqtt->state <= BSP_MQTT_STATE_WAIT_PINGRESP)) &&
      ((mqtt->wifi->tcp_state == BSP_WIFI_TCP_STATE_CLOSED) ||
       (mqtt->wifi->tcp_state == BSP_WIFI_TCP_STATE_ERROR)))
  {
    BSP_MQTT_EnterBackoff(mqtt, BSP_MQTT_ERROR_TCP, now_ms);
    return;
  }

  /* 每次调用最多推进一个发送阶段，不在主循环内等待 AT 或服务器响应。 */
  switch (mqtt->state)
  {
    case BSP_MQTT_STATE_WAIT_KEY:
      break;

    case BSP_MQTT_STATE_WAIT_WIFI:
      /* Wi-Fi 在线后只提交 TCP 连接请求，连接结果由 Wi-Fi BSP 异步更新。 */
      if ((mqtt->connect_requested != 0U) &&
          (mqtt->wifi->state == BSP_WIFI_STATE_ONLINE) &&
          (BSP_MQTT_TimeReached(now_ms, mqtt->next_action_ms) != 0U) &&
          (BSP_WiFi_TcpConnect(mqtt->wifi,
                               mqtt->broker_host,
                               mqtt->broker_port,
                               now_ms) == HAL_OK))
      {
        mqtt->state = BSP_MQTT_STATE_WAIT_TCP;
        mqtt->deadline_ms = now_ms + BSP_MQTT_RESPONSE_TIMEOUT_MS * 2U;
      }
      break;

    case BSP_MQTT_STATE_WAIT_TCP:
      if (mqtt->wifi->tcp_state == BSP_WIFI_TCP_STATE_OPEN)
      {
        mqtt->state = BSP_MQTT_STATE_SEND_CONNECT;
      }
      else if (BSP_MQTT_TimeReached(now_ms, mqtt->deadline_ms) != 0U)
      {
        BSP_MQTT_EnterBackoff(mqtt, BSP_MQTT_ERROR_TCP, now_ms);
      }
      break;

    case BSP_MQTT_STATE_SEND_CONNECT:
      if (mqtt->wifi->tcp_state == BSP_WIFI_TCP_STATE_OPEN)
      {
        mqtt->packet_length = BSP_MQTT_BuildConnect(mqtt);
        if ((mqtt->packet_length == 0U) ||
            (BSP_WiFi_TcpSend(mqtt->wifi,
                              mqtt->packet,
                              mqtt->packet_length,
                              now_ms) != HAL_OK))
        {
          BSP_MQTT_EnterBackoff(mqtt,
                                BSP_MQTT_ERROR_PACKET_TOO_LARGE,
                                now_ms);
        }
        else
        {
          mqtt->connect_attempt_count++;
          mqtt->packet_length = 0U;
          mqtt->state = BSP_MQTT_STATE_WAIT_CONNACK;
          mqtt->deadline_ms = now_ms + BSP_MQTT_RESPONSE_TIMEOUT_MS;
          mqtt->last_tx_ms = now_ms;
        }
      }
      break;

    case BSP_MQTT_STATE_SEND_SUBSCRIBE:
      if (mqtt->wifi->tcp_state == BSP_WIFI_TCP_STATE_OPEN)
      {
        mqtt->packet_length = BSP_MQTT_BuildSubscribe(mqtt);
        if ((mqtt->packet_length == 0U) ||
            (BSP_WiFi_TcpSend(mqtt->wifi,
                              mqtt->packet,
                              mqtt->packet_length,
                              now_ms) != HAL_OK))
        {
          BSP_MQTT_EnterBackoff(mqtt,
                                BSP_MQTT_ERROR_PACKET_TOO_LARGE,
                                now_ms);
        }
        else
        {
          mqtt->packet_length = 0U;
          mqtt->state = BSP_MQTT_STATE_WAIT_SUBACK;
          mqtt->deadline_ms = now_ms + BSP_MQTT_RESPONSE_TIMEOUT_MS;
          mqtt->last_tx_ms = now_ms;
        }
      }
      break;

    case BSP_MQTT_STATE_ONLINE:
      /* 遥测优先于心跳；持续发布时无需额外发送 PINGREQ。 */
      if ((mqtt->publish_pending != 0U) &&
          (mqtt->wifi->tcp_state == BSP_WIFI_TCP_STATE_OPEN))
      {
        mqtt->packet_length = BSP_MQTT_BuildPublish(mqtt);
        mqtt->expected_tcp_send_count = mqtt->wifi->tcp_send_count;
        if ((mqtt->packet_length > 0U) &&
            (BSP_WiFi_TcpSend(mqtt->wifi,
                              mqtt->packet,
                              mqtt->packet_length,
                              now_ms) == HAL_OK))
        {
          mqtt->packet_length = 0U;
          mqtt->state = BSP_MQTT_STATE_WAIT_PUBLISH_RESULT;
          mqtt->deadline_ms = now_ms + BSP_MQTT_RESPONSE_TIMEOUT_MS;
        }
      }
      else if (BSP_MQTT_TimeReached(
                   now_ms, mqtt->last_tx_ms + BSP_MQTT_KEEPALIVE_MS) != 0U)
      {
        mqtt->state = BSP_MQTT_STATE_SEND_PING;
      }
      break;

    case BSP_MQTT_STATE_WAIT_PUBLISH_RESULT:
      /* QoS 0 无 PUBACK，以 ESP 返回 SEND OK 作为本地发送完成依据。 */
      if ((mqtt->wifi->tcp_state == BSP_WIFI_TCP_STATE_OPEN) &&
          (mqtt->wifi->tcp_send_count != mqtt->expected_tcp_send_count))
      {
        mqtt->publish_pending = 0U;
        mqtt->publish_success_count++;
        mqtt->last_tx_ms = now_ms;
        mqtt->last_error = BSP_MQTT_ERROR_NONE;
        mqtt->state = BSP_MQTT_STATE_ONLINE;
      }
      else if (BSP_MQTT_TimeReached(now_ms, mqtt->deadline_ms) != 0U)
      {
        BSP_MQTT_EnterBackoff(mqtt, BSP_MQTT_ERROR_TIMEOUT, now_ms);
      }
      break;

    case BSP_MQTT_STATE_SEND_PING:
      if (mqtt->wifi->tcp_state == BSP_WIFI_TCP_STATE_OPEN)
      {
        mqtt->packet[0] = 0xC0U;
        mqtt->packet[1] = 0x00U;
        if (BSP_WiFi_TcpSend(mqtt->wifi,
                             mqtt->packet,
                             2U,
                             now_ms) == HAL_OK)
        {
          mqtt->state = BSP_MQTT_STATE_WAIT_PINGRESP;
          mqtt->deadline_ms = now_ms + BSP_MQTT_RESPONSE_TIMEOUT_MS;
          mqtt->last_tx_ms = now_ms;
        }
      }
      break;

    case BSP_MQTT_STATE_BACKOFF:
      if (BSP_MQTT_TimeReached(now_ms, mqtt->next_action_ms) != 0U)
      {
        mqtt->state = BSP_MQTT_STATE_WAIT_WIFI;
      }
      break;

    default:
      break;
  }
}


uint8_t BSP_MQTT_TakeMessage(BSP_MQTT_HandleTypeDef *mqtt,
                             char *message,
                             uint16_t message_size)
{
  if ((mqtt == NULL) || (message == NULL) || (message_size == 0U) ||
      (mqtt->message_pending == 0U))
  {
    return 0U;
  }
  (void)snprintf(message, message_size, "%s", mqtt->last_message);
  mqtt->message_pending = 0U;
  return 1U;
}
