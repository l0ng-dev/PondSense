# PondSense RA4M2

PondSense 是一个智能鱼类投喂项目。本分支保存基于 Renesas RA4M2 的固件工程，目前聚焦水温与 pH 信号采集。`dev/ra4m2` 与仓库 `main` 分支采用独立提交历史，供 RA4M2 方案继续开发。

## 当前实现

- **水温**：使用 P408 上的单总线驱动单只外部供电的 DS18B20，包含温度转换、读取、CRC 校验及错误状态处理。
- **pH 信号**：通过 P014／ADC0 AN012 读取原始 ADC 值，并进行采样窗口过滤。代码提供未校准的临时 pH 估算值；它不能作为已校准的水质测量结果。
- **应用调度**：使用 AGT0 提供毫秒时间基，在无 RTOS 的主循环中调度采样并记录状态。

当前工程尚未包含 Wi-Fi／MQTT、视觉通信、显示或投喂电机的应用实现。

## 工程位置

| 路径 | 内容 |
| --- | --- |
| `pondsence_RA4M2/pondsence_RA4M2.uvprojx` | Keil 工程 |
| `pondsence_RA4M2/configuration.xml` | RA Smart Configurator 配置 |
| `pondsence_RA4M2/src/app/` | 采样调度与状态管理 |
| `pondsence_RA4M2/src/modules/ds18b20/` | DS18B20 驱动 |
| `pondsence_RA4M2/ra/`、`pondsence_RA4M2/ra_cfg/`、`pondsence_RA4M2/ra_gen/` | 工程使用的 FSP 组件与生成文件 |

## 打开与构建

工程配置记录的器件为 `R7FA4M2AD3CFL`，FSP 版本为 `6.6.0`，Keil 工程使用 ARM Compiler 6.24。使用 Keil µVision 打开 `pondsence_RA4M2/pondsence_RA4M2.uvprojx`。如需重新生成配置或构建，请先安装与工程配置兼容的 Renesas RA Smart Configurator，并核对 `pondsence_RA4M2/rasc_version.txt` 中的本机安装路径及 Keil 工具链配置。仓库保留了工程引用的配置和依赖文件；本 README 的编写未执行构建验证。

## 硬件与数据边界

DS18B20 数据线需要符合目标板供电条件的外部上拉。连接 pH 模块前，应确认其输出电压和 ADC 输入范围相容，并完成实际标定。源码和静态配置不能证明传感器通信、电气连接或整机功能已经通过实测。
