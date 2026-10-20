/**
 * @file feed_motor.c
 * @brief 两路方向 GPIO 的限时电机驱动；未显式启动时保持停止。
 */
#include "feed_motor.h"

#include "hal_data.h"

#define FEED_IN1_PIN BSP_IO_PORT_05_PIN_00
#define FEED_IN2_PIN BSP_IO_PORT_04_PIN_02

volatile bsp_feed_motor_status_t g_feed_motor_status = {.last_error = FSP_SUCCESS};

/* 即使第一路写入失败，也尝试将另一路拉低；错误状态不能证明物理引脚已安全。 */
static fsp_err_t feed_motor_outputs_low(void)
{
    fsp_err_t const first = R_IOPORT_PinWrite(&g_ioport_ctrl, FEED_IN1_PIN, BSP_IO_LEVEL_LOW);
    fsp_err_t const second = R_IOPORT_PinWrite(&g_ioport_ctrl, FEED_IN2_PIN, BSP_IO_LEVEL_LOW);
    return (FSP_SUCCESS != first) ? first : second;
}

fsp_err_t BSP_FeedMotor_Init(void)
{
    uint32_t const interrupt_state = __get_PRIMASK();
    __disable_irq();
    g_feed_motor_status.initialized = false;
    g_feed_motor_status.running = false;
    g_feed_motor_status.start_ms = 0U;
    g_feed_motor_status.duration_ms = 0U;
    g_feed_motor_status.timeout_count = 0U;
    fsp_err_t const result = feed_motor_outputs_low();
    g_feed_motor_status.last_error = result;
    g_feed_motor_status.initialized = (FSP_SUCCESS == result);
    __set_PRIMASK(interrupt_state);
    return result;
}

fsp_err_t BSP_FeedMotor_Start(uint32_t now_ms, uint32_t duration_ms)
{
    if ((0U == duration_ms) || (duration_ms > BSP_FEED_MOTOR_MAX_RUN_MS))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    uint32_t const interrupt_state = __get_PRIMASK();
    __disable_irq();
    if (!g_feed_motor_status.initialized)
    {
        __set_PRIMASK(interrupt_state);
        return FSP_ERR_NOT_OPEN;
    }
    if (g_feed_motor_status.running)
    {
        __set_PRIMASK(interrupt_state);
        return FSP_ERR_IN_USE;
    }

    fsp_err_t result = R_IOPORT_PinWrite(&g_ioport_ctrl, FEED_IN2_PIN, BSP_IO_LEVEL_LOW);
    if (FSP_SUCCESS == result)
    {
        result = R_IOPORT_PinWrite(&g_ioport_ctrl, FEED_IN1_PIN, BSP_IO_LEVEL_HIGH);
    }
    if (FSP_SUCCESS == result)
    {
        g_feed_motor_status.start_ms = now_ms;
        g_feed_motor_status.duration_ms = duration_ms;
        g_feed_motor_status.running = true;
    }
    else
    {
        (void) feed_motor_outputs_low();
        g_feed_motor_status.initialized = false;
    }
    g_feed_motor_status.last_error = result;
    __set_PRIMASK(interrupt_state);
    return result;
}

fsp_err_t BSP_FeedMotor_Stop(void)
{
    uint32_t const interrupt_state = __get_PRIMASK();
    __disable_irq();
    fsp_err_t const result = feed_motor_outputs_low();
    g_feed_motor_status.running = false;
    g_feed_motor_status.duration_ms = 0U;
    g_feed_motor_status.last_error = result;
    if (FSP_SUCCESS != result)
    {
        g_feed_motor_status.initialized = false;
    }
    __set_PRIMASK(interrupt_state);
    return result;
}

void BSP_FeedMotor_Process(uint32_t now_ms)
{
    if (g_feed_motor_status.running &&
        ((uint32_t) (now_ms - g_feed_motor_status.start_ms) >= g_feed_motor_status.duration_ms))
    {
        ++g_feed_motor_status.timeout_count;
        (void) BSP_FeedMotor_Stop();
    }
}

static void feed_motor_fault_stop(void)
{
    __disable_irq();
    R_PORT5->PCNTR3 = (1UL << 16U);
    R_PORT4->PCNTR3 = (1UL << 18U);
    BSP_CFG_HANDLE_UNRECOVERABLE_ERROR(0);
    for (;;)
    {
        __WFI();
    }
}

void HardFault_Handler(void)
{
    feed_motor_fault_stop();
}

void MemManage_Handler(void)
{
    feed_motor_fault_stop();
}

void BusFault_Handler(void)
{
    feed_motor_fault_stop();
}

void UsageFault_Handler(void)
{
    feed_motor_fault_stop();
}
