# 第三方源码与修改记录

本工程从 MAR1801 配套 `Demo_STM32F407ZGT6_Software_SPI` 示例中筛选出启动文件、CMSIS 与 STM32 器件支持文件，并另写被动采集入口。没有导入其 LCD、触摸或其他外设示例源码。

## ARM CMSIS

- 文件：`CORE/core_cm4.h`、`core_cm4_simd.h`、`core_cmFunc.h`、`core_cmInstr.h`。
- 原文件版本：CMSIS V3.20；头部保留 ARM LIMITED 的版权、三条再分发条件和免责声明。
- 许可原文位于各文件头部，另在 [LICENSES/ARM-CMSIS-NOTICE.txt](LICENSES/ARM-CMSIS-NOTICE.txt) 保存同一声明。
- 本地兼容修改：`core_cmFunc.h` 在 Clang 分支使用 `__builtin_arm_set_fpscr`，避免旧 GCC 内联汇编的 `vfpcc` clobber 与 Arm Compiler 6 不兼容。其他编译器分支保留。

## STMicroelectronics

- 文件：`CORE/startup_stm32f40_41xxx.s`、`USER/stm32f4xx.h`、`system_stm32f4xx.c`、`system_stm32f4xx.h`。
- 原文件保留 STMicroelectronics 的版权及 `MCD-ST Liberty SW License Agreement V2` 声明。器件头文件注明 V1.4.0、2014-08-04。
- 许可入口为原头部所列 [ST 软件许可链接](https://www.st.com/software_license_agreement_liberty_v2)。核查时该链接跳转到 SLA0044 Rev5；它是当前链接内容，不能不加区分地当成上述历史 V2 附件。
- 本工程没有删除原声明，也没有将 ST 支持文件改标为 MIT 或其他项目统一许可。使用或再分发应按各原文件适用许可处理。
- 配置沿用测试板的 8 MHz HSE、`PLL_M=8`；这些是本板配置，不是全部 STM32F407 板的默认条件。

## 项目新增文件

`USER/main.c` 为本次排查编写的被动采集实现；工程文件和 README 用于构建、接线及记录采集限制。本文不为仓库原创文件新增统一许可，也不扩大第三方文件的授权范围。
