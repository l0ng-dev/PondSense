/**
 ******************************************************************************
 * @file    network_config.h
 * @brief   PondSense网络配置入口（可公开）
 *
 * @details
 * 本地开发时可在同目录创建 network_config.local.h。该文件不会纳入 Git，
 * 且应使用与本文件相同的宏名称。支持 __has_include 的编译器会优先载入
 * 本地配置；公开仓库和未配置环境使用下方的安全默认值。
 ******************************************************************************
 */

#ifndef NETWORK_CONFIG_H

#if defined(__has_include)
#if __has_include("network_config.local.h")
#include "network_config.local.h"
#else
#define PATROL_ROBOT_USE_PUBLIC_NETWORK_CONFIG
#endif
#else
#define PATROL_ROBOT_USE_PUBLIC_NETWORK_CONFIG
#endif

#ifdef PATROL_ROBOT_USE_PUBLIC_NETWORK_CONFIG
#define NETWORK_CONFIG_H

/** @brief 公开默认配置不自动连接网络。 */
#define WIFI_AUTO_CONNECT       0U
#define WIFI_DEFAULT_SSID       ""
#define WIFI_DEFAULT_PASSWORD   ""

/* 服务地址不是秘密；主题使用无账号关联的示例值。 */
#define MQTT_BROKER_HOST       "bemfa.com"
#define MQTT_BROKER_PORT       9501U
#define MQTT_BASE_TOPIC        "example_topic"
#define MQTT_K230_EVENT_TOPIC  "example_event"

#define MQTT_AUTO_CONNECT      0U
#define MQTT_TELEMETRY_INTERVAL_MS 5000U

#undef PATROL_ROBOT_USE_PUBLIC_NETWORK_CONFIG
#endif

#endif /* NETWORK_CONFIG_H */
