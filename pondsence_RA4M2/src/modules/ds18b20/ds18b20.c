/**
 * @file ds18b20.c
 * @brief P408 上的单只 DS18B20 单总线驱动。
 * @note 板载 R26 上拉到 USB_3V3；实际供电和 DQ 上拉仍需实物核对。
 *       每次读取九字节 scratchpad，并用第九字节校验前八字节的 CRC。
 */
#include "ds18b20.h"

#include "hal_data.h"

#define DS18B20_PIN           BSP_IO_PORT_04_PIN_08
#define DS18B20_CONVERSION_MS 750U
#define DS18B20_MIN_CENTI_C   (-5500)
#define DS18B20_MAX_CENTI_C   12500

static bsp_ds18b20_result_t bus_write(bsp_io_level_t level)
{
    return (FSP_SUCCESS == R_IOPORT_PinWrite(&g_ioport_ctrl, DS18B20_PIN, level)) ?
           BSP_DS18B20_OK : BSP_DS18B20_IO_ERROR;
}

static bsp_ds18b20_result_t bus_read(bsp_io_level_t * level)
{
    return (FSP_SUCCESS == R_IOPORT_PinRead(&g_ioport_ctrl, DS18B20_PIN, level)) ?
           BSP_DS18B20_OK : BSP_DS18B20_IO_ERROR;
}

static void delay_us(uint32_t us)
{
    R_BSP_SoftwareDelay(us, BSP_DELAY_UNITS_MICROSECONDS);
}

static void restore_interrupts(uint32_t primask)
{
    if (0U == primask)
    {
        __enable_irq();
    }
}

static bsp_ds18b20_result_t reset_and_detect(void)
{
    bsp_io_level_t level;
    bsp_ds18b20_result_t result = bus_read(&level);
    if (BSP_DS18B20_OK != result)
    {
        return result;
    }
    if (BSP_IO_LEVEL_HIGH != level)
    {
        return BSP_DS18B20_BUS_LOW;
    }

    result = bus_write(BSP_IO_LEVEL_LOW);
    if (BSP_DS18B20_OK != result)
    {
        return result;
    }
    delay_us(480U);

    /* Protect the presence sample from interrupt latency. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    result = bus_write(BSP_IO_LEVEL_HIGH); /* Open-drain high releases DQ. */
    if (BSP_DS18B20_OK == result)
    {
        delay_us(70U);
        result = bus_read(&level);
    }
    restore_interrupts(primask);
    if (BSP_DS18B20_OK != result)
    {
        return result;
    }

    delay_us(410U);
    bsp_io_level_t idle_level;
    result = bus_read(&idle_level);
    if (BSP_DS18B20_OK != result)
    {
        return result;
    }
    if (BSP_IO_LEVEL_HIGH != idle_level)
    {
        return BSP_DS18B20_BUS_LOW;
    }

    return (BSP_IO_LEVEL_LOW == level) ? BSP_DS18B20_OK : BSP_DS18B20_NO_DEVICE;
}

static bsp_ds18b20_result_t write_bit(bool one)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    bsp_ds18b20_result_t result = bus_write(BSP_IO_LEVEL_LOW);
    if (BSP_DS18B20_OK == result)
    {
        delay_us(one ? 6U : 60U);
        result = bus_write(BSP_IO_LEVEL_HIGH);
        if (BSP_DS18B20_OK == result)
        {
            delay_us(one ? 64U : 10U);
        }
    }
    restore_interrupts(primask);
    return result;
}

static bsp_ds18b20_result_t read_bit(bool * one)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    bsp_ds18b20_result_t result = bus_write(BSP_IO_LEVEL_LOW);
    if (BSP_DS18B20_OK == result)
    {
        delay_us(2U);
        result = bus_write(BSP_IO_LEVEL_HIGH);
        if (BSP_DS18B20_OK == result)
        {
            delay_us(8U);
            bsp_io_level_t level;
            result = bus_read(&level);
            if (BSP_DS18B20_OK == result)
            {
                *one = (BSP_IO_LEVEL_HIGH == level);
                delay_us(55U);
            }
        }
    }
    restore_interrupts(primask);
    return result;
}

static bsp_ds18b20_result_t write_byte(uint8_t value)
{
    for (uint8_t bit = 0U; bit < 8U; ++bit)
    {
        bsp_ds18b20_result_t result = write_bit((value & 1U) != 0U);
        if (BSP_DS18B20_OK != result)
        {
            return result;
        }
        value >>= 1U;
    }
    return BSP_DS18B20_OK;
}

static bsp_ds18b20_result_t read_byte(uint8_t * value)
{
    *value = 0U;
    for (uint8_t bit = 0U; bit < 8U; ++bit)
    {
        bool one;
        bsp_ds18b20_result_t result = read_bit(&one);
        if (BSP_DS18B20_OK != result)
        {
            return result;
        }
        if (one)
        {
            *value |= (uint8_t) (1U << bit);
        }
    }
    return BSP_DS18B20_OK;
}

static uint8_t crc8(uint8_t const * data, uint8_t length)
{
    uint8_t crc = 0U;
    for (uint8_t index = 0U; index < length; ++index)
    {
        uint8_t value = data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            bool mix = ((crc ^ value) & 1U) != 0U;
            crc >>= 1U;
            if (mix)
            {
                crc ^= 0x8CU;
            }
            value >>= 1U;
        }
    }
    return crc;
}

bsp_ds18b20_result_t BSP_DS18B20_Init(bsp_ds18b20_t * sensor)
{
    if (NULL == sensor)
    {
        return BSP_DS18B20_BAD_ARGUMENT;
    }

    sensor->initialized = false;
    sensor->conversion_pending = false;
    sensor->conversion_started_ms = 0U;

    /* Only drive low; writing high leaves the externally pulled-up bus released. */
    uint32_t const cfg = IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                         IOPORT_CFG_PORT_OUTPUT_HIGH |
                         IOPORT_CFG_NMOS_ENABLE;
    if (FSP_SUCCESS != R_IOPORT_PinCfg(&g_ioport_ctrl, DS18B20_PIN, cfg))
    {
        return BSP_DS18B20_IO_ERROR;
    }

    sensor->initialized = true;
    return BSP_DS18B20_OK;
}

bsp_ds18b20_result_t BSP_DS18B20_StartConversion(bsp_ds18b20_t * sensor, uint32_t now_ms)
{
    if (NULL == sensor)
    {
        return BSP_DS18B20_BAD_ARGUMENT;
    }
    if (!sensor->initialized)
    {
        return BSP_DS18B20_NOT_INITIALIZED;
    }
    if (sensor->conversion_pending)
    {
        return BSP_DS18B20_BUSY;
    }

    bsp_ds18b20_result_t result = reset_and_detect();
    if (BSP_DS18B20_OK != result)
    {
        return result;
    }
    result = write_byte(0xCCU); /* Skip ROM: one sensor on the bus. */
    if (BSP_DS18B20_OK == result)
    {
        result = write_byte(0x44U); /* Convert T. */
    }
    if (BSP_DS18B20_OK == result)
    {
        sensor->conversion_started_ms = now_ms;
        sensor->conversion_pending = true;
    }
    return result;
}

bsp_ds18b20_result_t BSP_DS18B20_ReadCentiC(bsp_ds18b20_t * sensor,
                                              uint32_t now_ms,
                                              int16_t * temperature_centi_c)
{
    if ((NULL == sensor) || (NULL == temperature_centi_c))
    {
        return BSP_DS18B20_BAD_ARGUMENT;
    }
    if (!sensor->initialized)
    {
        return BSP_DS18B20_NOT_INITIALIZED;
    }
    if (!sensor->conversion_pending)
    {
        return BSP_DS18B20_NO_CONVERSION;
    }
    if ((uint32_t) (now_ms - sensor->conversion_started_ms) < DS18B20_CONVERSION_MS)
    {
        return BSP_DS18B20_NOT_READY;
    }

    /* now_ms 由调用方在发送 Convert T 前取得，再读一次完成位以避免提前读取。 */
    bool conversion_done = false;
    bsp_ds18b20_result_t result = read_bit(&conversion_done);
    if (BSP_DS18B20_OK != result)
    {
        sensor->conversion_pending = false;
        return result;
    }
    if (!conversion_done)
    {
        return BSP_DS18B20_NOT_READY;
    }

    sensor->conversion_pending = false;
    result = reset_and_detect();
    if (BSP_DS18B20_OK != result)
    {
        return result;
    }
    result = write_byte(0xCCU);
    if (BSP_DS18B20_OK == result)
    {
        result = write_byte(0xBEU); /* Read Scratchpad. */
    }
    if (BSP_DS18B20_OK != result)
    {
        return result;
    }

    uint8_t scratchpad[9];
    for (uint8_t index = 0U; index < 9U; ++index)
    {
        result = read_byte(&scratchpad[index]);
        if (BSP_DS18B20_OK != result)
        {
            return result;
        }
    }
    if (crc8(scratchpad, 8U) != scratchpad[8])
    {
        return BSP_DS18B20_CRC_ERROR;
    }

    int16_t const raw = (int16_t) ((uint16_t) scratchpad[0] | ((uint16_t) scratchpad[1] << 8U));
    int32_t const centi_c = ((int32_t) raw * 100) / 16;
    if ((centi_c < DS18B20_MIN_CENTI_C) || (centi_c > DS18B20_MAX_CENTI_C))
    {
        return BSP_DS18B20_RANGE_ERROR;
    }
    *temperature_centi_c = (int16_t) centi_c;
    return BSP_DS18B20_OK;
}
