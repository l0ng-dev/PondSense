#ifndef APP_NETWORK_CONFIG_H
#define APP_NETWORK_CONFIG_H

/* 本地配置包含 Wi-Fi 密码及 MQTT Client ID，已由 .gitignore 排除。 */
#if defined(__has_include)
#if __has_include("network.local.h")
#include "network.local.h"
#endif
#endif

#ifndef APP_NETWORK_WIFI_SSID
#define APP_NETWORK_WIFI_SSID ""
#endif
#ifndef APP_NETWORK_WIFI_PASSWORD
#define APP_NETWORK_WIFI_PASSWORD ""
#endif
#ifndef APP_NETWORK_MQTT_HOST
#define APP_NETWORK_MQTT_HOST ""
#endif
#ifndef APP_NETWORK_MQTT_PORT
#define APP_NETWORK_MQTT_PORT 0U
#endif
#ifndef APP_NETWORK_MQTT_TOPIC
#define APP_NETWORK_MQTT_TOPIC ""
#endif
#ifndef APP_NETWORK_MQTT_CLIENT_ID
#define APP_NETWORK_MQTT_CLIENT_ID ""
#endif
#ifndef APP_NETWORK_TELEMETRY_INTERVAL_MS
#define APP_NETWORK_TELEMETRY_INTERVAL_MS 5000U
#endif

#endif
