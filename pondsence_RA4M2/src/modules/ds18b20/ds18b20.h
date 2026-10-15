#ifndef DS18B20_H
#define DS18B20_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    BSP_DS18B20_OK = 0,
    BSP_DS18B20_BAD_ARGUMENT,
    BSP_DS18B20_NOT_INITIALIZED,
    BSP_DS18B20_BUS_LOW,
    BSP_DS18B20_NO_DEVICE,
    BSP_DS18B20_BUSY,
    BSP_DS18B20_NO_CONVERSION,
    BSP_DS18B20_NOT_READY,
    BSP_DS18B20_CRC_ERROR,
    BSP_DS18B20_IO_ERROR,
    BSP_DS18B20_RANGE_ERROR
} bsp_ds18b20_result_t;

typedef struct
{
    uint32_t conversion_started_ms;
    bool initialized;
    bool conversion_pending;
} bsp_ds18b20_t;

/**
 * @brief 初始化单只外部供电的 DS18B20；仅重置软件中的待处理状态。
 * @note 如果上一次转换已启动，重新发起转换前仍应等待该次转换结束。
 *       单总线时隙会短暂忙等，不能从 ISR 调用；DQ 需要外部上拉。
 * @param sensor 由调用方持有并独占的实例。
 * @return 驱动状态；成功不表示探头已接入或通过存在检测。
 */
bsp_ds18b20_result_t BSP_DS18B20_Init(bsp_ds18b20_t * sensor);

/**
 * @brief 启动转换，等待由调用方在后续循环中推进。
 * @param now_ms 单调递增的毫秒时刻，允许 uint32_t 回绕。
 * @return BUSY 表示前一次结果尚未读取；失败后可重试。
 */
bsp_ds18b20_result_t BSP_DS18B20_StartConversion(bsp_ds18b20_t * sensor, uint32_t now_ms);

/**
 * @brief 经过至少 750 ms 且器件报告转换完成后，读取温度并校验数据。
 * @param now_ms 与 StartConversion 使用相同的单调毫秒时基。
 * @param temperature_centi_c 成功时写入 0.01 摄氏度单位的有符号温度。
 * @return NO_CONVERSION 表示未启动；NOT_READY 表示仍在转换。
 *         CRC 或温度范围异常及读取失败会清除待处理状态，调用方可重新启动转换。
 * @note 转换一直未完成时，调用方负责超时处理并调用 Init 重置软件状态。
 */
bsp_ds18b20_result_t BSP_DS18B20_ReadCentiC(bsp_ds18b20_t * sensor,
                                              uint32_t now_ms,
                                              int16_t * temperature_centi_c);

#endif
