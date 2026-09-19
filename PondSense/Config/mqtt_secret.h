/**
 ******************************************************************************
 * @file    mqtt_secret.h
 * @brief   PondSense MQTT 私钥配置入口（可公开）
 *
 * @details
 * 本地开发时在同目录创建 mqtt_secret.local.h，并在其中定义
 * MQTT_BEMFA_PRIVATE_KEY。该本地文件已由仓库根目录 .gitignore 排除。
 ******************************************************************************
 */

#ifndef MQTT_SECRET_H

#if defined(__has_include)
#if __has_include("mqtt_secret.local.h")
#include "mqtt_secret.local.h"
#else
#define PATROL_ROBOT_USE_PUBLIC_MQTT_SECRET
#endif
#else
#define PATROL_ROBOT_USE_PUBLIC_MQTT_SECRET
#endif

#ifdef PATROL_ROBOT_USE_PUBLIC_MQTT_SECRET
#define MQTT_SECRET_H
#define MQTT_BEMFA_PRIVATE_KEY ""
#undef PATROL_ROBOT_USE_PUBLIC_MQTT_SECRET
#endif

#endif /* MQTT_SECRET_H */
