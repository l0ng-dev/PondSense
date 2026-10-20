#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#include "sensing.h"
#include "feeding.h"

/**
 * @brief 初始化显示层及 OLED 驱动，清空本模块独占的显示缓存。
 * @note GPIO 初始化完成后，由主循环调用一次。
 */
void App_Display_Init(void);

/**
 * @brief 读取采样快照，按固定周期绘制界面，并推进 OLED 分块传输。
 * @param now_ms 单调递增的毫秒时刻，允许 uint32_t 回绕。
 * @param temperature 本轮温度快照，只读且仅在本次调用期间使用。
 * @param ph 本轮 pH 快照，只读且仅在本次调用期间使用。
 * @param feeding 本轮投喂状态，只读且仅在本次调用期间使用。
 * @note 三个快照均由主循环拥有，不允许在调用期间被中断修改。
 *       任一指针为空时直接返回；本接口不从中断调用。
 */
void App_Display_Process(uint32_t now_ms,
                         app_temperature_snapshot_t const * temperature,
                         app_ph_adc_snapshot_t const * ph,
                         app_feeding_status_t const * feeding);

#endif
