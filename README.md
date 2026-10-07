# PondSense

PondSense 是基于 STM32F407ZGT6 的智能鱼类投喂器原型仓库。这里保存 STM32 侧固件、CubeMX/Keil 工程、主机测试和历史交接记录；K230 发送端程序、模型、运行时及本地凭据不在本仓库。

## 项目范围与证据

STM32 侧包含温度与 pH 采集、LCD 显示、按键、电机投喂、K230 UART 结果接收，以及 ESP-01S/MQTT 通信。实现、接口和验证边界见项目架构说明.md及智能鱼类投喂器项目交接报告_2026-09-12.md。历史构建日志和主机测试不能替代接线、电气、传感器校准、机构重复性或长期现场验收。

## 工程入口

- CubeMX 配置源：PondSense/PondSense.ioc
- Keil 工程：PondSense/MDK-ARM/PondSense.uvprojx
- 主机测试：PondSense/Tests/Host/
- 本地网络配置示例：PondSense/Config/network_config.local.example.h

真实 Wi-Fi 和 MQTT 凭据只应放在 PondSense/Config/ 下被 .gitignore 排除的 *.local.h 文件中，不要提交到源码、文档、日志或 Issue。构建前请核对目标板、工具链和实际接线；不要根据仓库名称推断硬件已通过现场验收。

## 许可证与第三方内容

权属明确、且 l0ng-dev 有权授权的自有代码和文档采用根目录的 MIT License，版权声明为 Copyright (c) 2026 l0ng-dev。

根目录 MIT License 不适用于 ST HAL、CMSIS、正点原子 LCD 驱动和字库，或其他权属尚未明确的第三方内容。ST HAL 与 CMSIS 保留各自目录中的原始许可证。PondSense/BSP/LCD/ 中标有正点原子版权的 lcd.c、lcd.h、lcd_controller.c 和 lcd_font.h 保留原版权声明；其公开再分发授权尚未核实，不视为由 l0ng-dev 按 MIT 授权。使用或再分发这些文件前，应取得适用于对应文件的授权依据。GitHub 显示的 MIT 标识只反映根目录许可证，不能代表整仓所有文件具有相同授权。
