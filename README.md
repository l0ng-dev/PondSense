# PondSense RA4M2

基于 Renesas RA4M2 的智能鱼类投喂固件。RA4M2 采集水温和 pH 模拟信号，驱动 OLED，借助 ESP-01S 发布 MQTT 遥测，并根据 K230 返回的摄食等级安排投喂电机动作。工程使用 FSP 6.6.0、RA Smart Configurator（RASC）、Keil MDK 和无 RTOS 主循环。

[项目架构说明](项目架构说明.md)介绍模块职责、引脚、通信协议、投喂状态机及维护边界；本页提供第三方获取工程所需的入口信息。

## 仓库内容

| 路径 | 用途 |
| --- | --- |
| `pondsence_RA4M2/configuration.xml` | RASC 的 MCU、引脚与外设配置源 |
| `pondsence_RA4M2/pondsence_RA4M2.uvprojx` | Keil `Target_1` 工程 |
| `pondsence_RA4M2/src/app/` | 调度、采样、显示、网络和投喂业务 |
| `pondsence_RA4M2/src/modules/` | DS18B20、OLED、电机和 UART 链路 |
| `pondsence_RA4M2/src/protocol/` | K230 文本协议和 MQTT 报文编解码 |
| `pondsence_RA4M2/ra/`、`ra_cfg/`、`ra_gen/`、`via/` | FSP、生成配置及构建输入 |
| `tools/F407_SPI_Capture/`、`st7735s_backup_2026-10-17/` | 历史诊断工具与显示方案归档；不参与 RA4M2 固件构建 |

配置中的完整器件型号为 `R7FA4M2AD3CFL`。目录名 `pondsence_RA4M2` 是现有工程路径；OLED 首行实际显示 `PondSence`。

## 当前行为

- P015 上的单只 DS18B20 采用外部供电、单总线读取和 scratchpad CRC 校验；P014/ADC0 AN12 提供 pH 原始值、五点滤波和**未校准**的临时估算。
- P000/P001 以开漏 GPIO 模拟 I²C，分块刷新四针 SSD1306 OLED；界面显示水温、带 `*` 标记的 pH 估算、投喂状态和最近一次有效等级。
- 按下 K4 后先执行一次 17 秒抽拉。最后一个抽拉循环开始约 8 秒后向 K230 请求观察；有效等级 0 结束本轮，等级 1/2 分别安排下一批 1/2 次完整抽拉，再重新识别。批次间隔 1 秒。完成后需复位才能重新按 K4 启动。
- 电机单次运行由 AGT0 的 1 ms 中断检查 17 秒期限。K230 无效结果、识别超时或电机错误会使投喂进入故障锁定。IWDT 配置为自动启动，主循环约每秒刷新。
- ESP-01S 使用 SCI0 的 AT/TCP 链路建立 MQTT 3.1.1 连接；固件订阅配置主题，向该主题的 `/up` 子主题发布水温、pH 和摄食等级。下行消息只记录，不控制电机。

**投喂说明：**17 秒是一次完整抽拉所需的设计时长，不是整轮上限。当前代码没有整轮累计抽拉次数或出料量上限；该值须在有饵料时测得单次出料量后确定。

## 获取、配置和构建

1. 安装 Keil MDK、适配 `R7FA4M2AD3CFL` 的 Renesas RA Device Pack，以及与本工程匹配的 RASC/FSP 6.6.0。现有本机构建使用 Arm Compiler 6.24；其他版本需自行核对兼容性。
2. 打开 `pondsence_RA4M2/pondsence_RA4M2.uvprojx`，选择 `Target_1`。工程的 Keil 构建前命令调用 RASC 生成，构建后命令生成 Smart Bundle。
3. Keil 构建前后命令使用工程路径宏定位脚本与配置，无需按克隆目录修改。`rasc_launcher.bat` 会调用 `rasc_version.bat` 查找已安装的 RASC，并在本机创建被 Git 忽略的 `rasc_version.txt`；若自动查找失败，先安装或选择适配本工程的 RASC，再检查该文件中的可执行文件路径。
4. 如需联网，在 `pondsence_RA4M2/src/app/` 下创建不会提交的 `network.local.h`，定义 `APP_NETWORK_WIFI_SSID`、`APP_NETWORK_WIFI_PASSWORD`、`APP_NETWORK_MQTT_HOST`、`APP_NETWORK_MQTT_PORT`、`APP_NETWORK_MQTT_TOPIC` 和 `APP_NETWORK_MQTT_CLIENT_ID`；可选定义 `APP_NETWORK_TELEMETRY_INTERVAL_MS`。接口和空默认值见 `network_config.h`。缺少有效本地配置时，联网状态机保持禁用。
5. 检查 RASC 生成结果与 Keil 编译、链接、HEX 转换和 Smart Bundle 的实际输出，再使用与板卡相符的工具单独烧录与验证。`Objects/`、`Listings/`、本地 RASC/联网配置及构建日志均被 Git 忽略。

不要将真实热点密码、MQTT 标识或包含这些值的固件产物提交到公开仓库。重新生成项目内容时，以 `configuration.xml` 为上游配置，并检查生成差异和 Keil 工程设置。

## 接线与验证边界

引脚、方向和协议见[架构说明的接口表](项目架构说明.md#7-硬件接口与引脚)。接线前应核对实际 PCB 版本、供电、信号电平、公共地及模块引脚方向；配置文件和本 README 不能替代实物测量。OLED 和 DS18B20 的开漏数据线需要合适的 3.3 V 上拉；pH 模块 PO 在接入 P014 前须确认全工况及上电瞬间的输入电压范围。

2026-10-20 的本地 `Target_1` 构建记录显示 RASC 生成、Keil 编译链接及 Smart Bundle 成功，**0 错误、0 警告**；Code=19964、RO-data=3720、RW-data=32、ZI-data=6612。构建日志和产物不随仓库发布。此前有短时 MCP 观察到在线遥测计数增长、K4 触发及 K230 ACK/RESULT；这些记录没有把已烧录固件与当前源码的哈希逐一对应，不能替代第三方复现。

目前没有 pH 标准液标定、带饵料的单次出料量和整轮上限数据，也没有可公开复核的长期压力测试记录。pH 估算值不应用作已校准水质数据。

## 历史资料

[TFT 显示异常排查与处置报告](TFT显示异常排查与处置报告_2026-10-17.md)记录已撤回的 TFT 方案；[F407 SPI 采集工具](tools/F407_SPI_Capture/README.md)和[ST7735S 归档](st7735s_backup_2026-10-17/README.md)仅用于追溯，不属于当前 RA4M2 运行路径。
