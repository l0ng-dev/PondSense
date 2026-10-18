# PondSense RA4M2

PondSense 是一个智能鱼类投喂项目。本分支保存基于 Renesas RA4M2 的固件工程，已接入水温、pH 信号采集、OLED、ESP-01S 联网、K230 识别结果和投喂电机控制。`dev/ra4m2` 与仓库 `main` 分支采用独立提交历史，供 RA4M2 方案继续开发。

## 当前实现

- **水温**：使用 P015 上的单总线驱动单只外部供电的 DS18B20，包含温度转换、读取、CRC 校验及错误状态处理。
- **pH 信号**：通过 P014／ADC0 AN012 读取原始 ADC 值，并进行采样窗口过滤。代码提供未校准的临时 pH 估算值；它不能作为已校准的水质测量结果。
- **应用调度**：使用 AGT0 提供毫秒时间基，在无 RTOS 的主循环中调度采样并记录状态。
- **OLED**：SSD1306 四针屏，软件 I²C，7 位地址 `0x3C`；P000 作 SCL、P001 作 SDA。分块刷新，每次主循环最多发送一个短事务，包含 ACK、超时及总线恢复检查。界面显示温度、带未校准 `*` 标记的 pH 估算、投喂状态和最近一次有效等级；现场确认实屏显示正常。
- **投喂控制**：K4（P013）启动首个单循环投喂；单循环软件运行时长为 17 秒，由 AGT0 中断检查限时停机。SCI2 通过 P302 TX／P301 RX 与 K230 通信，接收等级 `0/1/2` 并决定结束或后续批次；识别超时、无效结果及电机异常进入故障锁定状态。
- **联网遥测**：SCI0 通过 P101 TX／P100 RX 连接 ESP-01S，应用执行 Wi-Fi、TCP 和 MQTT 3.1.1 连接与重连；订阅配置主题，并向该主题的 `/up` 子主题发布遥测。MQTT 下行只记录消息，不控制电机。

当前投喂流程由用户确认实机通过。固件尚无累计投喂循环上限和已启用的独立看门狗；等级 `0` 后保持完成状态，重新按 K4 不会开启新一轮。以上是现阶段行为，不能作为定量出料安全保证。

## OLED 接线（软件 I²C）

以下针号依据 RA4M2-SENSOR V1.0 原理图，断电并按板上 CN4 标号核对方向后接线：

| OLED 引脚 | RA 接点 |
| --- | --- |
| VCC | 3V3 |
| GND | 公共 GND |
| SCL | P000，CN4 第 6 针 |
| SDA | P001，CN4 第 7 针 |

OLED 信号线应接 P000/P001。P000/P001 配置为输出高、N 沟道开漏，高电平表示释放总线；需有到 3.3 V 的外部上拉，模块资料中的两路上拉为 4.7 kΩ。板上 P407 接有 C19（图纸标注 1 µF）及电压检测网络，不用于 OLED。SCI0/P100/P101、SCI2/P301/P302 继续预留给 ESP-01S 和 K230。

烧录后观察 `g_oled_status`：正常应 `initialized=1`、`last_error=FSP_SUCCESS`，`frame_count` 随刷新增长。`nack_count` 表示无应答，`last_nack_byte=0` 表示地址无应答，`timeout_count` 表示 SCL 未能升高，`bus_error_count` 表示 SDA 空闲或发送高位时仍为低。当前诊断接口为 `g_oled_status`。

## 工程位置

| 路径 | 内容 |
| --- | --- |
| `pondsence_RA4M2/pondsence_RA4M2.uvprojx` | Keil 工程 |
| `pondsence_RA4M2/configuration.xml` | RA Smart Configurator 配置 |
| `pondsence_RA4M2/src/app/app_main.c` | 应用调度、pH 估算与状态管理 |
| `pondsence_RA4M2/src/app/app_display.c` | 显示缓存、数值排版、投喂状态与 OLED 刷新调度 |
| `pondsence_RA4M2/src/app/app_feeding.c`、`app_k230.c` | 自动投喂状态机与 K230 事务管理 |
| `pondsence_RA4M2/src/app/app_network.c`、`app_network_config.h` | ESP-01S 联网、MQTT 状态机及公开配置入口 |
| `pondsence_RA4M2/src/modules/feed_motor/`、`src/modules/uart_rx/`、`src/protocol/k230/` | 电机、双路 UART 接收和 K230 协议 |
| `pondsence_RA4M2/src/modules/ds18b20/` | DS18B20 驱动 |
| `pondsence_RA4M2/src/modules/ssd1306/` | OLED 驱动与 `ssd1306_font.h` 字库 |
| `pondsence_RA4M2/ra/`、`pondsence_RA4M2/ra_cfg/`、`pondsence_RA4M2/ra_gen/` | 工程使用的 FSP 组件与生成文件 |

## 打开与构建

工程配置记录的器件为 `R7FA4M2AD3CFL`，FSP 版本为 `6.6.0`，Keil 工程使用 ARM Compiler 6.24。使用 Keil µVision 打开 `pondsence_RA4M2/pondsence_RA4M2.uvprojx`。如需重新生成配置或构建，请先安装与工程配置兼容的 Renesas RA Smart Configurator，并核对 `pondsence_RA4M2/rasc_version.txt` 中的本机安装路径及 Keil 工具链配置。当前 `Target_1_build.log` 记录 2026-10-18 的 RASC 生成、Keil 编译链接和 Smart Bundle 生成：0 错误、0 警告，Code=19784、RO=3440、RW=36、ZI=6940。构建产物在 `pondsence_RA4M2/Objects/`；构建记录不能代替烧录回执或实机测试记录。

联网参数由 Git 忽略的 `pondsence_RA4M2/src/app/app_network.local.h` 提供；仓库内的 `app_network_config.h` 只保留配置宏入口和空默认值。本地凭据、含凭据的固件产物及构建目录不要提交。

## 模块设计

- `app_main.c/.h`：采样调度、pH 估算及状态管理。
- `app_display.c/.h`：显示缓存、数值排版与刷新调度。
- `app_feeding.c/.h`、`app_k230.c/.h`：投喂批次、K230 观察结果与故障状态。
- `app_network.c/.h`：ESP-01S AT/TCP/MQTT 状态机与遥测。
- `feed_motor.c/.h`、`uart_rx.c/.h`、`protocol/k230/`：电机限时、串口接收与 K230 协议处理。
- `ssd1306.c/.h`：软件 I²C 与 SSD1306 页式显示驱动。公共头文件通过 `bsp_api.h` 引入必要类型，具体硬件配置依赖 `hal_data.h` 位于 `.c` 中。
- `ssd1306_font.h`：95 个 ASCII 字符和 21 个 16×16 中文字模，仅支持已收录汉字；来源说明保留在文件中。

RA 生成文件应通过 RASC 配置与生成流程维护，避免手工修改生成代码及注释。

## 验证状态与已知限制

测试环境：RA4M2-SENSOR V1.0、SSD1306 四针 OLED、3.3 V 供电，软件 I²C 使用上述接线。以下实机结果来自现场测试人员反馈。

| 检查项目 | 结果及范围 |
| --- | --- |
| OLED 显示与刷新 | 软件 I²C 版本正常显示并刷新；用户确认新增投喂状态和等级的界面实屏正常 |
| OLED 诊断 | 先前软件 I²C 版本的 Watch 记录为 `initialized=1`、`last_error=FSP_SUCCESS`、`frame_count=4`，错误、NACK、超时和总线错误计数均为 0；该记录不是全部版本的长期统计 |
| 联网及投喂 | 先前 MCP 观察到短时联网遥测与 K230 ACK／RESULT；用户确认投喂闭环、供电及电机电流检查、断网恢复与连续运行通过，具体次数、时长和测量值未记录 |
| 构建 | 当前 Target_1 构建日志为 0 错误、0 警告；该日志和产物被 Git 忽略，不随源码提交 |
| 早期静态分析 | OLED 模块拆分阶段，ARMCLANG 目标静态分析覆盖 6 个 C 文件，未报告诊断；未覆盖现阶段全部新增模块，也不代表完整 MISRA 合规认证 |
| 早期主机侧对照 | 5001 轮旧/新显示调用序列对照通过；该临时验证程序未随仓库提供，不能作为可直接复跑的测试套件 |

已知限制：

- pH 为未校准估算，尚不能作为准确水质测量结果。
- 暂无饵料和 pH 标准液，单循环出料量、最大允许出料量及 pH 精度未完成定量标定；这些暂不作为当前软件阶段的阻塞项。
- pH 窗口不稳定时可能继续显示上一笔估算，尚无过期标记。该行为已在提取函数的主机用例中复现，未修复。
- 分块刷新与界面重绘共用帧缓冲，存在新旧画面片段混合的风险。这是源码检查发现的风险，尚无实机异常记录，未实现帧快照或双缓冲。
- 尚无带测试时长、次数、固件哈希和测量值的可复核连续运行或整机验收记录；用户反馈不等同于量化测试记录。

## 硬件与数据边界

DS18B20 数据线需要符合目标板供电条件的外部上拉。连接 pH 模块前，应确认其输出电压和 ADC 输入范围相容，并完成实际标定。源码和静态配置不能证明传感器通信、电气连接或整机功能已经通过实测。

## 问题排查记录

- [TFT 显示异常排查与处置报告](TFT显示异常排查与处置报告_2026-10-17.md)：记录花屏转白屏、RS 链路排查、板载负载分析及停止 TFT 方案的结果；根因未最终确认。
- [F407 被动 SPI 采集工具](tools/F407_SPI_Capture/README.md)：独立调试工程、接线、数据格式与采集限制。
- [ST7735S 历史驱动归档](st7735s_backup_2026-10-17/README.md)：未通过 RA 显示验收的驱动快照，不参与 OLED 构建。
