# ST7735S 历史驱动归档

此目录保留 RA4M2 接入 MAR1801/ST7735S TFT 时使用的一份驱动快照，归档日期为 2026-10-17。

**状态：RA 实机显示未通过验收，故障未定位；不参与当前 OLED 工程编译。**

复位后，TFT 短暂花屏或显示部分字符，随后白屏。RS 路径异常尚未定位，详见 [TFT 排查报告](../TFT显示异常排查与处置报告_2026-10-17.md)。

## 文件与依赖

- [st7735s.c](st7735s.c)：软件 SPI、初始化表、清屏和有限字符集绘制。
- [st7735s.h](st7735s.h)：接口声明；显示尺寸 128×160。
- 依赖 RA FSP 的 `hal_data.h`、`g_ioport_ctrl`、GPIO API 和软件延时。
- 原 GPIO：CLK=P102、SDA=P103、RS=P104、RST=P013、CS=P015。
- `TFT_VENDOR_INIT_DIAGNOSTIC=0` 选择该快照的原初始化分支；代码保留了另一组厂家示例参数，并包含 SWRESET 后 120 ms 等待。

归档不含完整历史工程、旧应用界面及 RASC 配置。恢复实验需另备匹配配置和接线；P015 现用于温度 DQ，详见[当前接口表](../项目架构说明.md#6-硬件接口)。

## 已核对的板载复用

- P104 经 4.7 kΩ 与板载 LED D6 接地，P103 也有同类 LED 支路。
- P013 具有 4.7 kΩ 上拉、10 nF 对地电容及按键 K4。
- 没有在原图中发现 P104 具有 P407/C19 那样的 1 µF 电容与分压负载；不能将后续 OLED 的冲突直接解释成 TFT 的根因。

## 来源与许可声明

- 原初始化分支参考 [LCDWIKI_SPI](https://github.com/lcdwiki/LCDWIKI_SPI) 的 ST7735S 初始化序列；上游为 MIT 许可，保留的上游许可文本见 [LICENSES/LCDWIKI_SPI-MIT.txt](LICENSES/LCDWIKI_SPI-MIT.txt)。
- 备用初始化表参考 MAR1801 配套 `Demo_STM32F103ZET6_Software_SPI/HARDWARE/LCD/lcd.c` 的 `LCD_Init()` 参数；资料入口为 [LCDWIKI MAR1801](https://www.lcdwiki.com/res/MAR1801/)。本目录未导入整套 STM32 厂商示例，来源说明不表示其全部内容均采用 MIT。
- GPIO 适配、有限字形和绘制接口属于本项目历史实现。本说明只记录来源，不为整个文件或整个仓库另行设定统一许可。
