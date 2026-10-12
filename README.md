# PondSense

PondSense 是基于 STM32F407ZGT6 的智能鱼类投喂器原型。本仓库公开 STM32 侧固件、CubeMX/Keil 工程、主机测试和阶段记录，供读者了解当前实现与验证边界。K230 发送端程序、模型、运行时及本地凭据不包含在仓库中。

## 项目组成

- **感知与显示：** STM32 侧包含温度采集、pH 采样与演示估算、LCD 显示和按键交互。
- **投喂与视觉联动：** 电机投喂流程接收 K230 经 UART 返回的摄食等级。K230 侧程序及模型不在本仓库，单独获取本仓库不能复现完整视觉链路。
- **联网：** ESP-01S 承担 STM32 侧的 Wi-Fi/MQTT 小数据通信；图片上传由外部 K230 侧负责。

这是原型工程。仓库中的源码、历史构建日志和主机测试分别证明不同范围的结果，不代表接线、电气安全、传感器校准、机械重复性或长期现场运行已经通过验收。阶段性实现和证据可见 [项目架构说明](./项目架构说明.md) 与 [历史交接报告](./智能鱼类投喂器项目交接报告_2026-09-12.md)；后者保留了开发过程中的原始记录口吻。

## 工程入口

- [CubeMX 配置源](./PondSense/PondSense.ioc)
- [Keil 工程](./PondSense/MDK-ARM/PondSense.uvprojx)
- [主机测试](./PondSense/Tests/Host/)
- [本地网络配置示例](./PondSense/Config/network_config.local.example.h)

真实 Wi-Fi 和 MQTT 凭据不随仓库发布。`PondSense/Config/` 下的 `*.local.h` 文件由 `.gitignore` 排除；复现工程时仍需依据实际板卡、模块和接线核对配置。

## 许可证与第三方内容

由 l0ng-dev 持有版权且有权授权的原创代码和文档采用根目录 [MIT License](./LICENSE)，版权声明为 `Copyright (c) 2026 l0ng-dev`。

MIT 授权不适用于 ST HAL、CMSIS、正点原子 LCD 驱动与字库，或其他权属尚未明确的第三方内容。ST HAL 与 CMSIS 保留各自目录中的许可证。`PondSense/BSP/LCD/` 下的 `lcd.c`、`lcd.h`、`lcd_controller.c` 和 `lcd_font.h` 保留正点原子版权声明；这些文件的公开再分发授权尚未核实，也不由 l0ng-dev 按 MIT 授权。GitHub 显示的 MIT 标识不代表仓库内所有文件具有相同授权。
