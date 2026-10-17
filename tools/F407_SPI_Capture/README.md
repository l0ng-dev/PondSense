# F407 被动 SPI 采集工具

此工程用于观察 RA4M2 发往 ST7735S TFT 的 SPI 数据。它是排查辅助工具，**不会驱动 TFT 显示**，也不参与当前 RA OLED 应用的构建。

一次历史测试采集到 47,920 字节，并发现 RS 采样异常；TFT 故障未最终定位。背景与证据限制见[排查报告](../../TFT显示异常排查与处置报告_2026-10-17.md)。本工具未经过采集速率上限或长期可靠性认证。

## 硬件与工具链

- 测试用板：M144Z-M4，STM32F407ZGT6，外部晶振 8 MHz，目标系统时钟 168 MHz。
- 工程：[PROJECT/F407_SPI_Capture.uvprojx](PROJECT/F407_SPI_Capture.uvprojx)，目标 `SPI_Capture`。
- 已用工具链：Keil MDK / Arm Compiler 6.24；工程指定 `Keil.STM32F4xx_DFP.3.1.1`。
- 本地构建输出位于 `OBJ/`，不随源码提交。
- `USER/stm32f4xx.h` 的 `HSE_VALUE` 与 `system_stm32f4xx.c` 的 `PLL_M` 按上述 8 MHz 板配置，换板不能直接假定时钟兼容。

## 接线

RA 继续驱动 TFT；F407 并联监听下列信号。下表 RA 引脚是 TFT 排查阶段的历史接线，不是当前 OLED 应用接线。

| 总线信号 | F407 采集脚 | 历史 RA 脚 |
|---|---|---|
| SCK / CLK | PB3，SPI1_SCK | P102 |
| MOSI / SDA | PB5，SPI1_MOSI | P103 |
| D/C / RS | PB13，GPIO 输入 | P104 |
| CS | PB15，GPIO 输入 | P015 |
| TFT RST | PB12，GPIO 输入 | P013 |
| 公共地 | GND | GND |

- 信号必须为与 F407 输入兼容的 3.3 V 电平；不要把 TFT 的 5 V 供电端当作信号端。
- 两板分别供电并共地，不通过采集线连接两板的 3.3 V、5 V 电源输出。
- 不接 MISO。采集信号脚没有输出用途；PB14 单独保持高，用于关闭测试板上 W25Q128 的片选。
- **先在信号线断开时下载此程序，再断电接入采集线。**不能让旧 F407 显示示例与 RA 同时主动驱动总线。
- 迁移到其他 F407 板前，应核对 PB3/PB5/PB12/PB13/PB15 的实际引出和板载负载。

## 构建与首次下载

1. 打开上述工程，选择 `SPI_Capture` 并 Rebuild。
2. 工程未预设物理下载器，且关闭了进入 Debug 前自动下载。首次使用，在 **Options for Target → Debug** 中按实际设备选择 ST-Link、CMSIS-DAP 或其他驱动。
3. 在对应 Settings 选择 SWD，初次可用 100 kHz；核对设备识别结果。复位方式应与实际 NRST 接线匹配。
4. 在 **Utilities** 选择使用当前目标调试器进行 Flash Programming；在 **Flash Download** 选择匹配 STM32F407ZG 的 `STM32F4xx 1MB Flash`，地址 `0x08000000`，启用 Program 与 Verify。是否下载后复位运行由本次采集步骤决定。
5. 保存本机配置后执行 Download，确认编程及校验成功，再启动程序。退出 Keil Debug，释放探针给其他调试工具。

调试器设置保存在本机工程选项中，不能把忽略的 `.uvoptx`、`.uvguix.*` 当成公共依赖提交。分享前应检查 `.uvprojx` 未被本机 IDE 写入探针标识或绝对路径。

## 触发顺序

1. 下载完成、断电接好并联线后，给两板上电。
2. 先复位 F407；`g_capture_state=1` 表示等待 TFT RST 变低。
3. 再复位 RA，RA 必须对 TFT RST 产生低→高变化；F407 随后开始接收。
4. 等待至少 10 秒。采集到 50,000 个字节时可提前停止。
5. 在结束状态读取 `g_capture_count` 和 `g_capture_records`；不要在正在接收时暂停或单步，否则会干扰采样。
6. 读取结果前保持 F407 供电，不复位、不重新下载。缓冲区位于 RAM，复位或断电会丢失结果。

上电期间的 RST 抖动或次序不正确可能触发提前终止；先确认状态，不要把零条记录当成 RA 没有发送。重新采集时重复 F407→RA 的复位顺序。

## 状态与数据格式

| `g_capture_state` | 含义 |
|---|---|
| 0 | 启动中 |
| 1 | 等待 TFT RST 低电平 |
| 2 | 等待 TFT RST 回到高电平 |
| 3 | 接收中，此时不要暂停 |
| 4 | 已收满 50,000 字节 |
| 5 | 预设 10 秒到时，已保留此前采集数据 |
| 6 | SPI 接收溢出，不能按无漏采数据使用 |
| 7 | 接收期间再次检测到 TFT RST 低电平 |
| 8 | SPI 模式错误 |

- `g_capture_records`：50,000 个 `uint16_t`，只有前 `g_capture_count` 条有效。
- 每条记录：bit 0–7 为 SPI 字节，bit 8 为 RS，bit 9 为 CS，bit 10 为 RST。
- SPI 为 mode 0、MSB first，由 SPI1 外设接收；内部 NSS 固定有效，**不会根据外部 CS 自动重置字节边界或丢弃数据**。存在 CS 高期间的时钟或非整字节传输时，应单独评估对齐。
- RS/CS/RST 是软件在 RXNE 后读取的电平，**不是与 SPI 边沿同步的波形**；字节边界附近可能已变化。
- `g_capture_last_sr`、`g_capture_last_gpio` 为结束时快照；`g_capture_elapsed_cycles` 与 `g_capture_clock_hz` 可用于换算采集时长。
- 等待复位触发阶段不计入 10 秒窗口；未报告 OVR 不能替代对全部时序和采样完整性的验证。

## 第三方源码

见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。保留 ARM/ST 文件原有版权与许可声明；本目录不是对第三方文件重新统一授权的声明。
