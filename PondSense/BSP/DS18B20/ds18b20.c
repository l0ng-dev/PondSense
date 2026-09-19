/**
 * @file    ds18b20.c
 * @brief   DS18B20 单总线温度传感器驱动实现。
 *
 * @details
 * 实现微秒级时序、转换启动、温度读取、CRC 校验和无设备处理。
 */

#include "ds18b20.h"

#define DS18B20_CONVERSION_MS 750U

/* 使用已配置的 1 MHz 定时器提供单总线微秒级时序。 */
static void delay_us(const BSP_DS18B20 *sensor, uint32_t duration)
{
    uint32_t start = __HAL_TIM_GET_COUNTER(sensor->timer_1mhz);
    while ((uint32_t)(__HAL_TIM_GET_COUNTER(sensor->timer_1mhz) - start) < duration) {
    }
}

static bool reset_and_detect(const BSP_DS18B20 *sensor)
{
    HAL_GPIO_WritePin(sensor->port, sensor->pin, GPIO_PIN_RESET);
    delay_us(sensor, 480U);

    /* The presence sample must not be delayed by a UART interrupt. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    HAL_GPIO_WritePin(sensor->port, sensor->pin, GPIO_PIN_SET);
    delay_us(sensor, 70U);
    bool present = HAL_GPIO_ReadPin(sensor->port, sensor->pin) == GPIO_PIN_RESET;
    if (primask == 0U) {
        __enable_irq();
    }
    delay_us(sensor, 410U);
    return present;
}

static void write_bit(const BSP_DS18B20 *sensor, bool one)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    HAL_GPIO_WritePin(sensor->port, sensor->pin, GPIO_PIN_RESET);
    delay_us(sensor, one ? 6U : 60U);
    HAL_GPIO_WritePin(sensor->port, sensor->pin, GPIO_PIN_SET);
    delay_us(sensor, one ? 64U : 10U);
    if (primask == 0U) {
        __enable_irq();
    }
}

static bool read_bit(const BSP_DS18B20 *sensor)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    HAL_GPIO_WritePin(sensor->port, sensor->pin, GPIO_PIN_RESET);
    delay_us(sensor, 6U);
    HAL_GPIO_WritePin(sensor->port, sensor->pin, GPIO_PIN_SET);
    delay_us(sensor, 9U);
    bool one = HAL_GPIO_ReadPin(sensor->port, sensor->pin) == GPIO_PIN_SET;
    delay_us(sensor, 55U);
    if (primask == 0U) {
        __enable_irq();
    }
    return one;
}

static void write_byte(const BSP_DS18B20 *sensor, uint8_t value)
{
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        write_bit(sensor, (value & 1U) != 0U);
        value >>= 1U;
    }
}

static uint8_t read_byte(const BSP_DS18B20 *sensor)
{
    uint8_t value = 0U;
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        if (read_bit(sensor)) {
            value |= (uint8_t)(1U << bit);
        }
    }
    return value;
}

static uint8_t crc8(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0U;
    for (uint8_t index = 0U; index < length; ++index) {
        uint8_t value = data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            uint8_t mix = (uint8_t)((crc ^ value) & 1U);
            crc >>= 1U;
            if (mix != 0U) {
                crc ^= 0x8CU;
            }
            value >>= 1U;
        }
    }
    return crc;
}

/**
 * @brief  初始化 DS18B20 句柄并启动 1 MHz 时基。
 * @param  sensor     DS18B20 句柄
 * @param  timer_1mhz 提供微秒时序的定时器
 * @param  port       单总线 GPIO 端口
 * @param  pin        单总线 GPIO 引脚
 * @retval DS18B20 驱动状态
 */
BSP_DS18B20_Result BSP_DS18B20_Init(BSP_DS18B20 *sensor,
                                    TIM_HandleTypeDef *timer_1mhz,
                                    GPIO_TypeDef *port, uint16_t pin)
{
    if (sensor == NULL || timer_1mhz == NULL || port == NULL || pin == 0U) {
        return BSP_DS18B20_BAD_ARGUMENT;
    }
    sensor->timer_1mhz = timer_1mhz;
    sensor->port = port;
    sensor->pin = pin;
    sensor->conversion_started_ms = 0U;
    sensor->conversion_pending = false;
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    return HAL_TIM_Base_Start(timer_1mhz) == HAL_OK ? BSP_DS18B20_OK
                                                    : BSP_DS18B20_HAL_ERROR;
}

/**
 * @brief  启动一次 DS18B20 温度转换。
 * @param  sensor DS18B20 句柄
 * @retval DS18B20 驱动状态
 * @note   转换完成前应通过 BSP_DS18B20_ReadCentiC() 轮询读取。
 */
BSP_DS18B20_Result BSP_DS18B20_StartConversion(BSP_DS18B20 *sensor)
{
    if (sensor == NULL || sensor->timer_1mhz == NULL) {
        return BSP_DS18B20_BAD_ARGUMENT;
    }
    sensor->conversion_pending = false;
    if (!reset_and_detect(sensor)) {
        return BSP_DS18B20_NO_DEVICE;
    }
    write_byte(sensor, 0xCCU); /* Skip ROM: exactly one sensor on the bus. */
    write_byte(sensor, 0x44U); /* Convert T. */
    sensor->conversion_started_ms = HAL_GetTick();
    sensor->conversion_pending = true;
    return BSP_DS18B20_OK;
}

/**
 * @brief  读取已完成转换的摄氏温度值。
 * @param  sensor  DS18B20 句柄
 * @param  centi_c 输出温度，单位为 0.01 摄氏度
 * @retval DS18B20 驱动状态
 */
BSP_DS18B20_Result BSP_DS18B20_ReadCentiC(BSP_DS18B20 *sensor,
                                          int16_t *temperature_centi_c)
{
    if (sensor == NULL || sensor->timer_1mhz == NULL || temperature_centi_c == NULL) {
        return BSP_DS18B20_BAD_ARGUMENT;
    }
    if (!sensor->conversion_pending ||
        (uint32_t)(HAL_GetTick() - sensor->conversion_started_ms) < DS18B20_CONVERSION_MS) {
        return BSP_DS18B20_NOT_READY;
    }
    sensor->conversion_pending = false;
    if (!reset_and_detect(sensor)) {
        return BSP_DS18B20_NO_DEVICE;
    }
    write_byte(sensor, 0xCCU);
    write_byte(sensor, 0xBEU); /* Read all scratchpad bytes, including CRC. */
    uint8_t scratchpad[9];
    for (uint8_t index = 0U; index < 9U; ++index) {
        scratchpad[index] = read_byte(sensor);
    }
    if (crc8(scratchpad, 8U) != scratchpad[8]) {
        return BSP_DS18B20_CRC_ERROR;
    }
    int16_t raw = (int16_t)((uint16_t)scratchpad[0] |
                            ((uint16_t)scratchpad[1] << 8U));
    *temperature_centi_c = (int16_t)(((int32_t)raw * 100) / 16);
    return BSP_DS18B20_OK;
}
