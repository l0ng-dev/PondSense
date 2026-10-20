# PondSense RA4M2

基于 Renesas RA4M2 的鱼类投喂固件。RA4M2 采集水温和 pH 模拟信号，驱动 OLED，通过 ESP-01S 发送 MQTT 遥测，并根据 K230 的识别结果控制投喂电机。工程使用 FSP 6.6.0、RASC、Keil MDK 和无 RTOS 主循环。

模块职责、引脚和通信流程见[项目架构说明](项目架构说明.md)。

## 工程内容

| 路径 | 内容 |
| --- | --- |
| `pondsence_RA4M2/configuration.xml` | RASC 配置；器件型号为 `R7FA4M2AD3CFL` |
| `pondsence_RA4M2/pondsence_RA4M2.uvprojx` | Keil `Target_1` 工程 |
| `pondsence_RA4M2/src/` | 应用、设备模块和协议代码 |
| `pondsence_RA4M2/ra/`、`ra_cfg/`、`ra_gen/`、`via/` | FSP 与生成文件 |

## 当前功能

- DS18B20 水温采集；P014/ADC0 AN12 采集 pH 信号，显示未校准估算值。
- SSD1306 OLED 显示水温、pH 估算和投喂状态，首行文字为 `PondSence`。
- K4 启动投喂。每次抽拉 17 秒；K230 返回等级 0 时结束，等级 1/2 分别安排下一批 1/2 次抽拉。完成或故障后需复位才能再次启动。
- ESP-01S 负责 Wi-Fi、TCP 和 MQTT 3.1.1 连接。下行消息仅记录，不控制电机。

固件尚无整轮累计出料上限；单次出料量及 pH 精度有待实物标定。

## 构建

1. 安装 Keil MDK、适配 `R7FA4M2AD3CFL` 的 Renesas RA Device Pack，以及兼容 FSP 6.6.0 的 RASC。本工程已用 Arm Compiler 6.24 构建。
2. 打开 `pondsence_RA4M2/pondsence_RA4M2.uvprojx`，选择 `Target_1` 并构建。Keil 构建前后分别执行 RASC 生成与 Smart Bundle 生成。RASC 安装位置由启动脚本检测，结果保存在被 Git 忽略的 `rasc_version.txt`。
3. 联网时，在 `pondsence_RA4M2/src/app/` 下创建被 Git 忽略的 `network.local.h`，按 [network_config.h](pondsence_RA4M2/src/app/network_config.h) 定义 Wi-Fi 和 MQTT 参数。缺少有效配置时联网保持禁用。

`Objects/`、`Listings/`、本地配置和构建日志不随仓库发布。不要提交热点密码或含凭据的固件产物。

## 接线与验证

接线前按[接口表](项目架构说明.md#6-硬件接口)核对板卡、供电、电平和引脚方向。OLED 的 SCL/SDA 及 DS18B20 数据线需要上拉到 3.3 V；pH 模块 PO 接入 P014 前应测量全工况及上电瞬间电压。

2026-10-20，从不含本地联网配置的源码快照构建 `Target_1`：RASC 生成、Keil 编译链接及 Smart Bundle 均成功，0 错误、0 警告。用户已烧录最新代码并反馈运行半小时正常；本仓库没有记录本次负载及各功能测量值。

历史资料：[TFT 排查报告](TFT显示异常排查与处置报告_2026-10-17.md)、[F407 SPI 采集工具](tools/F407_SPI_Capture/README.md)、[ST7735S 驱动归档](st7735s_backup_2026-10-17/README.md)。这些内容不参与当前固件构建。
