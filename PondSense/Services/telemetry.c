/**
 * @file    telemetry.c
 * @brief   ESP-01S/MQTT 遥测服务实现。
 *
 * @details
 * 组织温度、pH 和摄食等级 JSON，并通过非阻塞 Wi-Fi/MQTT 状态机定期发布。
 * 网络状态不参与本地电机投喂决策。
 */

#include "telemetry.h"
#include "mqtt_client.h"
#include "mqtt_secret.h"
#include "network_config.h"
#include <stddef.h>
#include <stdio.h>

static BSP_WiFi_HandleTypeDef telemetry_wifi;
static BSP_MQTT_HandleTypeDef telemetry_mqtt;

static uint8_t initialized;
static uint32_t next_publish_ms;

/* 生成固定字段的中文遥测 JSON，不写入任何凭据。 */
static int format_message(char *buffer,
                          size_t buffer_size,
                          const SensingSnapshot *sensors,
                          uint8_t result_valid,
                          uint8_t result_level)
{
  char temperature_value[16];
  char ph_value[16];
  char result_value[8];
  int32_t magnitude;
  int written;

  if ((buffer == NULL) || (buffer_size == 0U) || (sensors == NULL))
  {
    return -1;
  }

  if (sensors->temperature_valid != 0U)
  {
    magnitude = (sensors->temperature_centi_c < 0)
                    ? -(int32_t)sensors->temperature_centi_c
                    : (int32_t)sensors->temperature_centi_c;
    (void)snprintf(temperature_value,
                   sizeof(temperature_value),
                   "\"%s%ld.%02ld℃\"",
                   sensors->temperature_centi_c < 0 ? "-" : "",
                   (long)(magnitude / 100L),
                   (long)(magnitude % 100L));
  }
  else
  {
    (void)snprintf(temperature_value, sizeof(temperature_value), "null");
  }

  if (sensors->ph_valid != 0U)
  {
    (void)snprintf(ph_value,
                   sizeof(ph_value),
                   "\"%u.%02u\"",
                   (unsigned int)(sensors->ph_centi / 100U),
                   (unsigned int)(sensors->ph_centi % 100U));
  }
  else
  {
    (void)snprintf(ph_value, sizeof(ph_value), "null");
  }

  if ((result_valid != 0U) && (result_level <= 2U))
  {
    (void)snprintf(result_value,
                   sizeof(result_value),
                   "%u",
                   (unsigned int)result_level);
  }
  else
  {
    (void)snprintf(result_value, sizeof(result_value), "null");
  }

  written = snprintf(buffer,
                     buffer_size,
                     "{\"水温\":%s,\"pH值\":%s,\"摄食等级\":%s}",
                     temperature_value,
                     ph_value,
                     result_value);
  if ((written <= 0) || ((size_t)written >= buffer_size))
  {
    buffer[0] = '\0';
    return -1;
  }
  return written;
}

/**
 * @brief  初始化 Wi-Fi/MQTT 遥测链路。
 * @param  uart   ESP-01S 所在 UART 句柄
 * @param  now_ms 当前系统毫秒时间
 * @retval HAL 状态
 */
HAL_StatusTypeDef Telemetry_Init(UART_HandleTypeDef *uart, uint32_t now_ms)
{
  if (BSP_WiFi_Init(&telemetry_wifi, uart, now_ms) != HAL_OK)
  {
    return HAL_ERROR;
  }

#if WIFI_AUTO_CONNECT
  if ((BSP_WiFi_SetSSID(&telemetry_wifi, WIFI_DEFAULT_SSID) != HAL_OK) ||
      (BSP_WiFi_SetPassword(&telemetry_wifi,
                            WIFI_DEFAULT_PASSWORD) != HAL_OK) ||
      (BSP_WiFi_Connect(&telemetry_wifi, now_ms) != HAL_OK))
  {
    return HAL_ERROR;
  }
#endif

  if (BSP_MQTT_Init(&telemetry_mqtt,
                    &telemetry_wifi,
                    MQTT_BROKER_HOST,
                    MQTT_BROKER_PORT,
                    MQTT_BASE_TOPIC,
                    now_ms) != HAL_OK)
  {
    return HAL_ERROR;
  }

#if MQTT_AUTO_CONNECT
  if ((BSP_MQTT_SetKey(&telemetry_mqtt,
                       MQTT_BEMFA_PRIVATE_KEY) != HAL_OK) ||
      (BSP_MQTT_Connect(&telemetry_mqtt, now_ms) != HAL_OK))
  {
    return HAL_ERROR;
  }
#endif

  next_publish_ms = now_ms + MQTT_TELEMETRY_INTERVAL_MS;
  initialized = 1U;
  return HAL_OK;
}

/* 推进网络状态机，并在发布周期到达时提交一条遥测消息。 */
/**
 * @brief  推进网络状态机并按周期发布遥测。
 * @param  now_ms              当前系统毫秒时间
 * @param  sensors              当前温度/pH 快照
 * @param  feeding_result_valid 摄食等级是否有效
 * @param  feeding_result_level 摄食等级 0/1/2
 */
void Telemetry_Process(uint32_t now_ms,
                       const SensingSnapshot *sensors,
                       uint8_t feeding_result_valid,
                       uint8_t feeding_result_level)
{
  char message[BSP_MQTT_MESSAGE_SIZE];
  char downlink[BSP_MQTT_MESSAGE_SIZE];

  if ((initialized == 0U) || (sensors == NULL))
  {
    return;
  }

  BSP_WiFi_Process(&telemetry_wifi, now_ms);
  BSP_MQTT_Process(&telemetry_mqtt, now_ms);
  (void)BSP_MQTT_TakeMessage(&telemetry_mqtt,
                             downlink,
                             sizeof(downlink));

  if ((telemetry_mqtt.state != BSP_MQTT_STATE_ONLINE) ||
      (telemetry_mqtt.publish_pending != 0U) ||
      ((int32_t)(now_ms - next_publish_ms) < 0))
  {
    return;
  }

  if (format_message(message,
                     sizeof(message),
                     sensors,
                     feeding_result_valid,
                     feeding_result_level) > 0)
  {
    (void)BSP_MQTT_RequestPublish(&telemetry_mqtt, message);
  }
  next_publish_ms = now_ms + MQTT_TELEMETRY_INTERVAL_MS;
}

void Telemetry_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
  BSP_WiFi_RxEventCallback(&telemetry_wifi, uart, size);
}

void Telemetry_ErrorCallback(UART_HandleTypeDef *uart)
{
  BSP_WiFi_ErrorCallback(&telemetry_wifi, uart);
}
