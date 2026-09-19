/**
 ******************************************************************************
 * @file    mqtt_client.h
 * @brief   基于 ESP-01S TCP 通道的 MQTT 3.1.1 客户端接口
 *
 * @details
 * 主要功能：
 * 1. 建立 MQTT 连接并订阅基础主题。
 * 2. 以 QoS 0 发布遥测数据，并接收服务器下行消息。
 * 3. 通过非阻塞状态机完成超时、心跳和退避重连。
 *
 * 模块关系：
 * PondSense 应用层调用本模块，本模块通过 BSP/ESP01S 的单 TCP 通道收发
 * MQTT 二进制报文。
 *
 * 主要接口：
 * BSP_MQTT_Init()、BSP_MQTT_SetKey()、BSP_MQTT_Connect()、
 * BSP_MQTT_RequestPublish() 和 BSP_MQTT_Process()。
 ******************************************************************************
 */

#ifndef BSP_MQTT_H
#define BSP_MQTT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp01s.h"
#include <stdint.h>

#define BSP_MQTT_KEY_MAX_LENGTH       64U
#define BSP_MQTT_TOPIC_MAX_LENGTH     48U
#define BSP_MQTT_PUBLISH_TOPIC_SIZE  (BSP_MQTT_TOPIC_MAX_LENGTH + 8U)
#define BSP_MQTT_PACKET_SIZE         256U
#define BSP_MQTT_MESSAGE_SIZE        160U

/** @brief MQTT 客户端非阻塞运行状态。 */
typedef enum
{
  BSP_MQTT_STATE_NOT_INITIALIZED = 0,
  BSP_MQTT_STATE_WAIT_KEY,
  BSP_MQTT_STATE_WAIT_WIFI,
  BSP_MQTT_STATE_WAIT_TCP,
  BSP_MQTT_STATE_SEND_CONNECT,
  BSP_MQTT_STATE_WAIT_CONNACK,
  BSP_MQTT_STATE_SEND_SUBSCRIBE,
  BSP_MQTT_STATE_WAIT_SUBACK,
  BSP_MQTT_STATE_ONLINE,
  BSP_MQTT_STATE_WAIT_PUBLISH_RESULT,
  BSP_MQTT_STATE_SEND_PING,
  BSP_MQTT_STATE_WAIT_PINGRESP,
  BSP_MQTT_STATE_BACKOFF
} BSP_MQTT_StateTypeDef;

/** @brief MQTT 客户端最近一次错误类型。 */
typedef enum
{
  BSP_MQTT_ERROR_NONE = 0,
  BSP_MQTT_ERROR_ARGUMENT,
  BSP_MQTT_ERROR_KEY_REQUIRED,
  BSP_MQTT_ERROR_TCP,
  BSP_MQTT_ERROR_PACKET_TOO_LARGE,
  BSP_MQTT_ERROR_PROTOCOL,
  BSP_MQTT_ERROR_CONNACK,
  BSP_MQTT_ERROR_SUBACK,
  BSP_MQTT_ERROR_TIMEOUT
} BSP_MQTT_ErrorTypeDef;

/** @brief MQTT 客户端运行句柄和固定容量收发缓冲区。 */
typedef struct
{
  BSP_WiFi_HandleTypeDef *wifi;
  BSP_MQTT_StateTypeDef state;
  BSP_MQTT_ErrorTypeDef last_error;
  uint8_t connect_requested;
  uint8_t key_configured;
  uint8_t publish_pending;
  uint8_t message_pending;
  uint8_t connack_code;
  uint8_t suback_code;
  uint16_t packet_id;
  uint16_t packet_length;
  uint32_t deadline_ms;
  uint32_t next_action_ms;
  uint32_t last_tx_ms;
  uint32_t expected_tcp_send_count;
  uint32_t connect_attempt_count;
  uint32_t connect_success_count;
  uint32_t subscribe_success_count;
  uint32_t reconnect_count;
  uint32_t rx_packet_count;
  uint32_t publish_request_count;
  uint32_t publish_success_count;
  uint32_t publish_failure_count;
  char broker_host[BSP_WIFI_TCP_HOST_MAX_LENGTH + 1U];
  uint16_t broker_port;
  char key[BSP_MQTT_KEY_MAX_LENGTH + 1U];
  char base_topic[BSP_MQTT_TOPIC_MAX_LENGTH + 1U];
  char publish_topic[BSP_MQTT_PUBLISH_TOPIC_SIZE];
  char pending_topic[BSP_MQTT_PUBLISH_TOPIC_SIZE];
  char pending_message[BSP_MQTT_MESSAGE_SIZE];
  char last_message[BSP_MQTT_MESSAGE_SIZE];
  uint8_t packet[BSP_MQTT_PACKET_SIZE];
} BSP_MQTT_HandleTypeDef;

/**
 * @brief  初始化 MQTT 客户端
 * @param  mqtt        MQTT 客户端句柄
 * @param  wifi        已初始化的 Wi-Fi 句柄
 * @param  broker_host MQTT 服务器域名或地址
 * @param  broker_port MQTT 服务器端口
 * @param  base_topic  订阅的基础主题；上行主题自动追加 /up
 * @param  now_ms      当前 HAL 毫秒计时
 * @retval HAL 状态
 */
HAL_StatusTypeDef BSP_MQTT_Init(BSP_MQTT_HandleTypeDef *mqtt,
                                BSP_WiFi_HandleTypeDef *wifi,
                                const char *broker_host,
                                uint16_t broker_port,
                                const char *base_topic,
                                uint32_t now_ms);
/**
 * @brief  设置 MQTT Client ID 使用的巴法云私钥
 * @param  mqtt MQTT 客户端句柄
 * @param  key  以空字符结尾的私钥字符串
 * @retval HAL 状态
 * @note   本接口不会输出或回显私钥。
 */
HAL_StatusTypeDef BSP_MQTT_SetKey(BSP_MQTT_HandleTypeDef *mqtt,
                                  const char *key);
/**
 * @brief  请求建立 MQTT 连接
 * @param  mqtt   MQTT 客户端句柄
 * @param  now_ms 当前 HAL 毫秒计时
 * @retval HAL 状态；实际连接由 BSP_MQTT_Process() 非阻塞推进
 */
HAL_StatusTypeDef BSP_MQTT_Connect(BSP_MQTT_HandleTypeDef *mqtt,
                                   uint32_t now_ms);
/**
 * @brief  请求向上行主题发布一条 QoS 0 消息
 * @param  mqtt    MQTT 客户端句柄
 * @param  message 以空字符结尾的消息正文
 * @retval HAL 状态；HAL_BUSY 表示已有一条消息等待发送
 */
HAL_StatusTypeDef BSP_MQTT_RequestPublish(BSP_MQTT_HandleTypeDef *mqtt,
                                           const char *message);
/**
 * @brief  推进 MQTT 收包、超时、连接、订阅、发布和心跳状态机
 * @param  mqtt   MQTT 客户端句柄
 * @param  now_ms 当前 HAL 毫秒计时
 * @note   应由主循环高频调用，不在本函数内阻塞等待网络响应。
 */
void BSP_MQTT_Process(BSP_MQTT_HandleTypeDef *mqtt, uint32_t now_ms);
/**
 * @brief  取出一条已经完整接收的下行消息
 * @param  mqtt         MQTT 客户端句柄
 * @param  message      消息输出缓冲区
 * @param  message_size 输出缓冲区容量
 * @retval 1 表示取到消息，0 表示当前没有待取消息或参数无效
 */
uint8_t BSP_MQTT_TakeMessage(BSP_MQTT_HandleTypeDef *mqtt,
                             char *message,
                             uint16_t message_size);
#ifdef __cplusplus
}
#endif

#endif /* BSP_MQTT_H */
