/**
 * @file    esp01s_internal.h
 * @brief   ESP-01S Wi-Fi 驱动内部接口。
 *
 * @details
 * 仅供 ESP01S 驱动的多个实现文件共享状态机、解析器和 TCP 内部函数，
 * 不作为应用层公共接口使用。
 */

#ifndef BSP_ESP01S_INTERNAL_H
#define BSP_ESP01S_INTERNAL_H

#include "esp01s.h"

#define BSP_WIFI_TX_TIMEOUT_MS           100U
#define BSP_WIFI_READY_WAIT_MS          2000U
#define BSP_WIFI_COMMAND_TIMEOUT_MS     1500U
#define BSP_WIFI_SCAN_TIMEOUT_MS       10000U
#define BSP_WIFI_JOIN_TIMEOUT_MS       20000U
#define BSP_WIFI_RETRY_DELAY_MS          300U
#define BSP_WIFI_MAX_STATE_RETRIES         3U
#define BSP_WIFI_MAX_BACKOFF_MS        10000U
#define BSP_WIFI_TCP_CONNECT_TIMEOUT_MS 10000U
#define BSP_WIFI_TCP_SEND_TIMEOUT_MS     3000U

void BSP_WiFi_SetState(BSP_WiFi_HandleTypeDef *wifi,
                       BSP_WiFi_StateTypeDef state,
                       uint32_t next_action_ms);
void BSP_WiFi_EnterBackoff(BSP_WiFi_HandleTypeDef *wifi, uint32_t now_ms);
void BSP_WiFi_CommandSucceeded(BSP_WiFi_HandleTypeDef *wifi, uint32_t now_ms);
void BSP_WiFi_CommandFailed(BSP_WiFi_HandleTypeDef *wifi,
                            BSP_WiFi_ErrorTypeDef error,
                            uint32_t now_ms);
HAL_StatusTypeDef BSP_WiFi_SendCommand(BSP_WiFi_HandleTypeDef *wifi,
                                       const char *command,
                                       uint32_t now_ms,
                                       uint32_t timeout_ms);
void BSP_WiFi_HandleLine(BSP_WiFi_HandleTypeDef *wifi,
                         const char *line,
                         uint32_t now_ms);
void BSP_WiFi_ProcessReceivedData(BSP_WiFi_HandleTypeDef *wifi,
                                  uint32_t now_ms);
void BSP_WiFi_ResetTcp(BSP_WiFi_HandleTypeDef *wifi);
void BSP_WiFi_TcpFailed(BSP_WiFi_HandleTypeDef *wifi,
                        BSP_WiFi_ErrorTypeDef error);
void BSP_WiFi_PushTcpRxByte(BSP_WiFi_HandleTypeDef *wifi, uint8_t value);
void BSP_WiFi_ProcessTcp(BSP_WiFi_HandleTypeDef *wifi, uint32_t now_ms);

#endif /* BSP_ESP01S_INTERNAL_H */
