/**
 ******************************************************************************
 * @file    esp01s.h
 * @brief   ESP-01S Wi-Fi AT 通信驱动接口
 *
 * @details
 * 主要功能：
 * 1. 使用 UART 空闲中断接收和环形缓冲区保存 ESP-01S 响应。
 * 2. 非阻塞执行 AT 同步、版本查询、Station 配置和热点连接。
 * 3. 管理单路 TCP 连接，解析 +IPD 二进制数据并提供独立接收环形缓冲区。
 * 4. 提供超时、有限重试和退避重连。
 *
 * @note
 * SSID 和密码只保存在 RAM 中。
 ******************************************************************************
 */

#ifndef BSP_ESP01S_H
#define BSP_ESP01S_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define BSP_WIFI_RX_CHUNK_SIZE        64U
#define BSP_WIFI_RX_RING_SIZE         512U
#define BSP_WIFI_LINE_SIZE            192U
#define BSP_WIFI_SSID_MAX_LENGTH      32U
#define BSP_WIFI_PASSWORD_MAX_LENGTH  63U
#define BSP_WIFI_TCP_HOST_MAX_LENGTH   63U
#define BSP_WIFI_TCP_TX_BUFFER_SIZE   256U
#define BSP_WIFI_TCP_RX_RING_SIZE     256U

/** @brief ESP-01S 单路 TCP 传输状态。 */
typedef enum
{
  BSP_WIFI_TCP_STATE_CLOSED = 0,
  BSP_WIFI_TCP_STATE_CONNECT_PENDING,
  BSP_WIFI_TCP_STATE_CONNECTING,
  BSP_WIFI_TCP_STATE_OPEN,
  BSP_WIFI_TCP_STATE_SEND_PENDING,
  BSP_WIFI_TCP_STATE_WAIT_PROMPT,
  BSP_WIFI_TCP_STATE_WAIT_SEND_RESULT,
  BSP_WIFI_TCP_STATE_ERROR
} BSP_WiFi_TcpStateTypeDef;

typedef enum
{
  BSP_WIFI_STATE_NOT_INITIALIZED = 0,
  BSP_WIFI_STATE_WAIT_READY = 1,
  BSP_WIFI_STATE_SYNC = 2,
  BSP_WIFI_STATE_ECHO_OFF = 3,
  BSP_WIFI_STATE_SET_STATION_MODE = 4,
  BSP_WIFI_STATE_WAIT_CREDENTIALS = 5,
  BSP_WIFI_STATE_JOINING = 6,
  BSP_WIFI_STATE_ONLINE = 7,
  BSP_WIFI_STATE_BACKOFF = 8,
  BSP_WIFI_STATE_ERROR = 9,
  BSP_WIFI_STATE_DISABLE_AUTO_CONNECT = 10,
  BSP_WIFI_STATE_DISCONNECT_AP = 11,
  BSP_WIFI_STATE_SCAN_TARGET_AP = 12,
  BSP_WIFI_STATE_SET_SINGLE_CONNECTION = 13
} BSP_WiFi_StateTypeDef;

typedef enum
{
  BSP_WIFI_ERROR_NONE = 0,
  BSP_WIFI_ERROR_INVALID_ARGUMENT,
  BSP_WIFI_ERROR_UART_RX_START,
  BSP_WIFI_ERROR_UART_TX,
  BSP_WIFI_ERROR_TIMEOUT,
  BSP_WIFI_ERROR_RESPONSE,
  BSP_WIFI_ERROR_BUSY,
  BSP_WIFI_ERROR_RX_OVERFLOW,
  BSP_WIFI_ERROR_CREDENTIALS,
  BSP_WIFI_ERROR_AP_NOT_FOUND,
  BSP_WIFI_ERROR_JOIN_FAILED,
  BSP_WIFI_ERROR_TCP_ARGUMENT,
  BSP_WIFI_ERROR_TCP_CONNECT,
  BSP_WIFI_ERROR_TCP_SEND,
  BSP_WIFI_ERROR_TCP_TIMEOUT,
  BSP_WIFI_ERROR_TCP_RX_OVERFLOW
} BSP_WiFi_ErrorTypeDef;

typedef struct
{
  UART_HandleTypeDef *uart;
  uint8_t rx_chunk[BSP_WIFI_RX_CHUNK_SIZE];
  volatile uint16_t rx_dma_position;
  volatile uint8_t rx_ring[BSP_WIFI_RX_RING_SIZE];
  volatile uint16_t rx_head;
  volatile uint16_t rx_tail;
  char line[BSP_WIFI_LINE_SIZE];
  uint16_t line_length;
  uint8_t discarding_line;
  volatile uint8_t rx_restart_pending;
  uint8_t awaiting_response;
  uint8_t got_ip;
  uint8_t ssid_set;
  uint8_t password_set;
  uint8_t connect_requested;
  uint8_t auto_connect_disabled;
  uint8_t target_ap_found;
  uint8_t join_error_code;
  uint8_t state_retry_count;
  uint8_t backoff_exponent;
  BSP_WiFi_StateTypeDef state;
  BSP_WiFi_ErrorTypeDef last_error;
  uint32_t command_deadline_ms;
  uint32_t next_action_ms;
  uint32_t command_tx_count;
  uint32_t command_success_count;
  uint32_t command_failure_count;
  uint32_t reconnect_count;
  uint32_t scan_cycle_count;
  uint32_t scan_ap_count;
  uint32_t scan_parse_error_count;
  int16_t target_ap_rssi;
  char ssid[BSP_WIFI_SSID_MAX_LENGTH + 1U];
  char password[BSP_WIFI_PASSWORD_MAX_LENGTH + 1U];
  BSP_WiFi_TcpStateTypeDef tcp_state;
  uint8_t tcp_prompt_received;
  uint8_t tcp_ipd_mode;
  uint16_t tcp_ipd_length;
  uint16_t tcp_ipd_received;
  uint16_t tcp_port;
  uint16_t tcp_tx_length;
  uint16_t tcp_rx_head;
  uint16_t tcp_rx_tail;
  uint32_t tcp_deadline_ms;
  uint32_t tcp_connect_attempt_count;
  uint32_t tcp_connect_count;
  uint32_t tcp_send_count;
  char tcp_host[BSP_WIFI_TCP_HOST_MAX_LENGTH + 1U];
  uint8_t tcp_tx_buffer[BSP_WIFI_TCP_TX_BUFFER_SIZE];
  uint8_t tcp_rx_ring[BSP_WIFI_TCP_RX_RING_SIZE];
} BSP_WiFi_HandleTypeDef;

HAL_StatusTypeDef BSP_WiFi_Init(BSP_WiFi_HandleTypeDef *wifi,
                                UART_HandleTypeDef *uart,
                                uint32_t now_ms);
void BSP_WiFi_Process(BSP_WiFi_HandleTypeDef *wifi, uint32_t now_ms);
HAL_StatusTypeDef BSP_WiFi_SetSSID(BSP_WiFi_HandleTypeDef *wifi,
                                   const char *ssid);
HAL_StatusTypeDef BSP_WiFi_SetPassword(BSP_WiFi_HandleTypeDef *wifi,
                                       const char *password);
HAL_StatusTypeDef BSP_WiFi_Connect(BSP_WiFi_HandleTypeDef *wifi,
                                   uint32_t now_ms);
/**
 * @brief  请求建立单路 TCP 连接
 * @param  wifi   Wi-Fi 驱动句柄
 * @param  host   服务器域名或地址
 * @param  port   服务器端口
 * @param  now_ms 当前 HAL 毫秒计时
 * @retval HAL 状态；实际 AT 流程由 BSP_WiFi_Process() 非阻塞推进
 */
HAL_StatusTypeDef BSP_WiFi_TcpConnect(BSP_WiFi_HandleTypeDef *wifi,
                                      const char *host,
                                      uint16_t port,
                                      uint32_t now_ms);
/**
 * @brief  请求通过已打开的 TCP 连接发送二进制数据
 * @param  wifi   Wi-Fi 驱动句柄
 * @param  data   待发送数据
 * @param  length 数据长度
 * @param  now_ms 当前 HAL 毫秒计时
 * @retval HAL 状态；HAL_BUSY 表示链路未就绪或前一笔发送未完成
 */
HAL_StatusTypeDef BSP_WiFi_TcpSend(BSP_WiFi_HandleTypeDef *wifi,
                                   const uint8_t *data,
                                   uint16_t length,
                                   uint32_t now_ms);
/**
 * @brief  关闭并清理本地 TCP 连接状态
 * @param  wifi Wi-Fi 驱动句柄
 * @retval HAL 状态
 */
HAL_StatusTypeDef BSP_WiFi_TcpClose(BSP_WiFi_HandleTypeDef *wifi);
/**
 * @brief  从 TCP 接收环形缓冲区读取二进制数据
 * @param  wifi           Wi-Fi 驱动句柄
 * @param  data           数据输出缓冲区
 * @param  maximum_length 本次允许读取的最大字节数
 * @retval 实际读取的字节数
 */
uint16_t BSP_WiFi_TcpRead(BSP_WiFi_HandleTypeDef *wifi,
                          uint8_t *data,
                          uint16_t maximum_length);
void BSP_WiFi_RxEventCallback(BSP_WiFi_HandleTypeDef *wifi,
                              UART_HandleTypeDef *uart,
                              uint16_t size);
void BSP_WiFi_ErrorCallback(BSP_WiFi_HandleTypeDef *wifi,
                            UART_HandleTypeDef *uart);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ESP01S_H */
