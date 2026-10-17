# ST7735S 历史驱动归档

此目录保留 RA4M2 接入 MAR1801/ST7735S TFT 时使用的一份驱动快照，归档日期为 2026-10-17。

**状态：未通过 RA 实机显示验收，故障未最终定位；不参与当前 OLED 工程编译。**这不是可直接用于生产或已经验证可用的显示组件。

故障为复位后短暂花屏或部分字符、随后白屏。软件初始化与传输方式对照、连接排查均未使 TFT 通过验收；RS 路径异常是重要线索，但没有确定失效点。完整过程见 [TFT 显示异常排查与处置报告](../TFT显示异常排查与处置报告_2026-10-17.md)。

## 文件与依赖

- [st7735s.c](st7735s.c)：软件 SPI、初始化表、清屏和有限字符集绘制。
- [st7735s.h](st7735s.h)：接口声明；显示尺寸 128×160。
- 依赖 RA FSP 的 `hal_data.h`、`g_ioport_ctrl`、GPIO API 和软件延时。
- 原 GPIO：CLK=P102、SDA=P103、RS=P104、RST=P013、CS=P015。
- `TFT_VENDOR_INIT_DIAGNOSTIC=0` 选择该快照的原初始化分支；代码保留了另一组厂家示例参数，并包含 SWRESET 后 120 ms 等待。

这两个文件不是独立可构建的历史工程，也不包含全部实验版本、旧应用界面及对应 RASC 配置。恢复实验时应单独准备匹配配置，不能直接添加到当前 OLED 工程，或沿用已改变用途的接线。P015 已用于温度 DQ，具体当前资源以根目录 README 为准。

## 已核对的板载复用

- P104 经 4.7 kΩ 与板载 LED D6 接地，P103 也有同类 LED 支路。
- P013 具有 4.7 kΩ 上拉、10 nF 对地电容及按键 K4。
- 没有在原图中发现 P104 具有 P407/C19 那样的 1 µF 电容与分压负载；不能将后续 OLED 的冲突直接解释成 TFT 的根因。

## 来源与许可声明

- 原初始化分支参考 [LCDWIKI_SPI](https://github.com/lcdwiki/LCDWIKI_SPI) 的 ST7735S 初始化序列；上游为 MIT 许可，保留的上游许可文本见 [LICENSES/LCDWIKI_SPI-MIT.txt](LICENSES/LCDWIKI_SPI-MIT.txt)。
- 备用初始化表参考 MAR1801 配套 `Demo_STM32F103ZET6_Software_SPI/HARDWARE/LCD/lcd.c` 的 `LCD_Init()` 参数；资料入口为 [LCDWIKI MAR1801](https://www.lcdwiki.com/res/MAR1801/)。本目录未导入整套 STM32 厂商示例，来源说明不表示其全部内容均采用 MIT。
- GPIO 适配、有限字形和绘制接口属于本项目历史实现。本说明只记录来源，不为整个文件或整个仓库另行设定统一许可。
