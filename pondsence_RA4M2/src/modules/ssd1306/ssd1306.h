#ifndef BSP_SSD1306_H
#define BSP_SSD1306_H

#include <stdbool.h>
#include <stdint.h>

/* FSP 6.6 的公共入口提供 fsp_err_t；具体外设实例只在实现文件中引用。 */
#include "bsp_api.h"

typedef struct
{
    fsp_err_t last_error;
    uint32_t error_count;
    uint32_t frame_count;
    bool initialized;
    uint32_t nack_count;
    uint32_t timeout_count;
    uint32_t bus_error_count;
    uint32_t last_nack_byte; /* UINT32_MAX 表示尚无无应答记录；0 为地址字节，1 为控制字节。 */
} bsp_ssd1306_status_t;

/* 状态仅由主循环更新，供调试器观察；volatile 不代表整个结构体可原子读取。 */
extern volatile bsp_ssd1306_status_t g_oled_status;

/**
 * @brief 清空显存和诊断状态，开始显示初始化状态机。
 * @note 调用前须已打开 IOPORT，并按 RASC 配置将 P000/P001 设为开漏输出高。
 *       本函数不立即完成屏幕初始化，后续需持续调用 Process 推进状态机。
 */
void BSP_SSD1306_Init(void);
/* 清空软件显存并标记待刷新，不立即访问屏幕。 */
void BSP_SSD1306_ClearBuffer(void);
/**
 * @brief 将 ASCII 或字库中的 UTF-8 汉字绘入显存，并标记待刷新。
 * @param row 行号，范围 0～3，每行 16 像素。
 * @param column 起始列号，范围 0～15，每列 8 像素，汉字占两列。
 * @param text 调用期间有效、以空字符结尾的字符串；函数不持有该指针。
 * @note 空指针或起始坐标非法时直接返回；超出右边界时截断。
 */
void BSP_SSD1306_DrawText(uint8_t row, uint8_t column, char const * text);
/**
 * @brief 在主循环中推进初始化、分块刷新及错误恢复，不得从中断调用。
 * @param now_ms 单调递增的毫秒时刻，允许 uint32_t 回绕。
 * @note 每次调用最多执行一个短同步事务；错误后释放总线并等待重试。
 *       所有绘图和刷新接口须由同一个主循环独占，不支持中断或多任务并发调用。
 */
void BSP_SSD1306_Process(uint32_t now_ms);

#endif
