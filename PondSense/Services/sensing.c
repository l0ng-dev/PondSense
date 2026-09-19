/**
 * @file    sensing.c
 * @brief   温度与 pH 传感器服务实现。
 *
 * @details
 * 协调 DS18B20 异步转换、ADC 采样、滑动窗口稳定性判断和显示文本生成。
 */

#include "sensing.h"
#include "ds18b20.h"
#include "ph_sensor.h"
#include <stddef.h>
#include <stdio.h>

#define SENSOR_STABILIZATION_MS       5000U
#define SAMPLE_PERIOD_MS              1000U
#define FILTER_WINDOW                 5U
#define TEMP_STABLE_MAX_SPAN_CENTI    50U
#define PH_STABLE_MAX_SPAN            20U
#define PH_ADC_LOW_LIMIT              10U
#define PH_ADC_HIGH_LIMIT             4085U
#define PH_REFERENCE_ADC              1539.0f
#define PH_REFERENCE_VALUE            6.8f
#define PH_ADC_REFERENCE_V            3.3f
#define PH_NOMINAL_SLOPE_PER_V        (-5.6342f)

static BSP_DS18B20 temperature_sensor;
static ADC_HandleTypeDef *ph_adc;
static uint32_t stabilization_start_ms;
static uint32_t temperature_next_ms;
static uint32_t ph_next_ms;
static uint8_t temperature_ready;
static uint8_t temperature_conversion_active;
static int16_t temperature_samples[FILTER_WINDOW];
static int32_t temperature_sum;
static uint8_t temperature_count;
static uint8_t temperature_index;
static uint16_t ph_samples[FILTER_WINDOW];
static uint32_t ph_sum;
static uint8_t ph_count;
static uint8_t ph_index;
static SensingSnapshot current;
static char temperature_text[24];
static char ph_text[24];

static void set_temperature_text(const char *text)
{
  (void)snprintf(temperature_text, sizeof(temperature_text), "%s", text);
}

static void set_ph_text(const char *text)
{
  (void)snprintf(ph_text, sizeof(ph_text), "%s", text);
}

/**
 * @brief  初始化温度和 pH 采样服务。
 * @param  timer            DS18B20 使用的 1 MHz 定时器
 * @param  temperature_port DS18B20 数据 GPIO 端口
 * @param  temperature_pin DS18B20 数据 GPIO 引脚
 * @param  adc             pH ADC 句柄
 * @param  now_ms          当前系统毫秒时间
 * @retval HAL 状态
 */
HAL_StatusTypeDef Sensing_Init(TIM_HandleTypeDef *timer,
                               GPIO_TypeDef *temperature_port,
                               uint16_t temperature_pin,
                               ADC_HandleTypeDef *adc,
                               uint32_t now_ms)
{
  if ((timer == NULL) || (temperature_port == NULL) || (adc == NULL))
  {
    return HAL_ERROR;
  }

  ph_adc = adc;
  stabilization_start_ms = now_ms;
  temperature_next_ms = now_ms;
  ph_next_ms = now_ms;
  current.temperature_valid = 0U;
  current.ph_valid = 0U;
  set_temperature_text("TEMP: WAIT");
  set_ph_text("PH: WAIT");

  temperature_ready =
      (uint8_t)(BSP_DS18B20_Init(&temperature_sensor,
                                 timer,
                                 temperature_port,
                                 temperature_pin) == BSP_DS18B20_OK);
  if (temperature_ready == 0U)
  {
    set_temperature_text("TEMP: TIMER ERROR");
    return HAL_ERROR;
  }
  return HAL_OK;
}

/* 启动转换与读取分离，避免 750 ms 转换时间阻塞主循环。 */
static void process_temperature(uint32_t now_ms)
{
  BSP_DS18B20_Result result;

  if ((temperature_ready != 0U) &&
      (temperature_conversion_active == 0U) &&
      ((int32_t)(now_ms - temperature_next_ms) >= 0))
  {
    result = BSP_DS18B20_StartConversion(&temperature_sensor);
    temperature_next_ms = now_ms + SAMPLE_PERIOD_MS;
    if (result == BSP_DS18B20_OK)
    {
      temperature_conversion_active = 1U;
    }
    else
    {
      current.temperature_valid = 0U;
      set_temperature_text(result == BSP_DS18B20_NO_DEVICE
                               ? "TEMP: NO DEVICE"
                               : "TEMP: ERROR");
    }
  }

  if (temperature_conversion_active != 0U)
  {
    int16_t sample;
    result = BSP_DS18B20_ReadCentiC(&temperature_sensor, &sample);
    if (result == BSP_DS18B20_NOT_READY)
    {
      return;
    }
    temperature_conversion_active = 0U;
    if (result != BSP_DS18B20_OK)
    {
      current.temperature_valid = 0U;
      set_temperature_text(result == BSP_DS18B20_NO_DEVICE
                               ? "TEMP: NO DEVICE"
                               : result == BSP_DS18B20_CRC_ERROR
                                     ? "TEMP: CRC ERROR"
                                     : "TEMP: ERROR");
      return;
    }

    if (temperature_count < FILTER_WINDOW)
    {
      temperature_samples[temperature_index] = sample;
      temperature_sum += sample;
      temperature_count++;
    }
    else
    {
      temperature_sum -= temperature_samples[temperature_index];
      temperature_samples[temperature_index] = sample;
      temperature_sum += sample;
    }
    temperature_index =
        (uint8_t)((temperature_index + 1U) % FILTER_WINDOW);

    if (temperature_count >= FILTER_WINDOW)
    {
      int16_t minimum = temperature_samples[0];
      int16_t maximum = temperature_samples[0];
      uint8_t index;
      int32_t magnitude;

      for (index = 1U; index < temperature_count; index++)
      {
        if (temperature_samples[index] < minimum)
        {
          minimum = temperature_samples[index];
        }
        if (temperature_samples[index] > maximum)
        {
          maximum = temperature_samples[index];
        }
      }
      current.temperature_centi_c =
          (int16_t)(temperature_sum / temperature_count);
      if ((uint16_t)((int32_t)maximum - (int32_t)minimum) >
          TEMP_STABLE_MAX_SPAN_CENTI)
      {
        current.temperature_valid = 0U;
        set_temperature_text("TEMP: UNSTABLE");
      }
      else
      {
        current.temperature_valid = 1U;
        magnitude = (current.temperature_centi_c < 0)
                        ? -(int32_t)current.temperature_centi_c
                        : (int32_t)current.temperature_centi_c;
        (void)snprintf(temperature_text,
                       sizeof(temperature_text),
                       "TEMP: %s%ld.%02ld C",
                       current.temperature_centi_c < 0 ? "-" : "",
                       (long)(magnitude / 100L),
                       (long)(magnitude % 100L));
      }
    }
  }
}

/* 以固定周期采样 ADC，并在窗口稳定后计算临时 pH 估计值。 */
static void process_ph(uint32_t now_ms)
{
  uint16_t sample;
  uint16_t minimum;
  uint16_t maximum;
  uint16_t average;
  uint8_t index;

  if ((int32_t)(now_ms - ph_next_ms) < 0)
  {
    return;
  }
  ph_next_ms = now_ms + SAMPLE_PERIOD_MS;

  if (!BSP_PH_ReadRaw(ph_adc, &sample))
  {
    current.ph_valid = 0U;
    set_ph_text("PH: ADC ERROR");
    return;
  }

  if (ph_count < FILTER_WINDOW)
  {
    ph_samples[ph_index] = sample;
    ph_sum += sample;
    ph_count++;
  }
  else
  {
    ph_sum -= ph_samples[ph_index];
    ph_samples[ph_index] = sample;
    ph_sum += sample;
  }
  ph_index = (uint8_t)((ph_index + 1U) % FILTER_WINDOW);

  if ((ph_count < FILTER_WINDOW) ||
      ((uint32_t)(now_ms - stabilization_start_ms) <
       SENSOR_STABILIZATION_MS))
  {
    current.ph_valid = 0U;
    set_ph_text("PH: WAIT");
    return;
  }

  average = (uint16_t)(ph_sum / ph_count);
  minimum = ph_samples[0];
  maximum = ph_samples[0];
  for (index = 1U; index < ph_count; index++)
  {
    if (ph_samples[index] < minimum)
    {
      minimum = ph_samples[index];
    }
    if (ph_samples[index] > maximum)
    {
      maximum = ph_samples[index];
    }
  }
  if ((average <= PH_ADC_LOW_LIMIT) || (average >= PH_ADC_HIGH_LIMIT))
  {
    current.ph_valid = 0U;
    set_ph_text("PH: INPUT LIMIT");
  }
  else if ((uint16_t)(maximum - minimum) > PH_STABLE_MAX_SPAN)
  {
    current.ph_valid = 0U;
    set_ph_text("PH: UNSTABLE");
  }
  else
  {
    float adc_delta_v = ((float)average - PH_REFERENCE_ADC) *
                        PH_ADC_REFERENCE_V / 4095.0f;
    float estimate = PH_REFERENCE_VALUE +
                     PH_NOMINAL_SLOPE_PER_V * adc_delta_v;
    if ((estimate >= 0.0f) && (estimate <= 14.0f))
    {
      current.ph_centi = (uint16_t)(estimate * 100.0f + 0.5f);
      current.ph_valid = 1U;
      (void)snprintf(ph_text,
                     sizeof(ph_text),
                     "PH: %u.%02u",
                     (unsigned int)(current.ph_centi / 100U),
                     (unsigned int)(current.ph_centi % 100U));
    }
    else
    {
      current.ph_valid = 0U;
      set_ph_text("PH: OUT OF RANGE");
    }
  }
}

/**
 * @brief  周期推进温度转换和 pH 采样。
 * @param  now_ms 当前系统毫秒时间
 */
void Sensing_Process(uint32_t now_ms)
{
  process_temperature(now_ms);
  process_ph(now_ms);
}

/**
 * @brief  复制当前传感器快照。
 * @param  snapshot 输出快照地址
 */
void Sensing_GetSnapshot(SensingSnapshot *snapshot)
{
  if (snapshot != NULL)
  {
    *snapshot = current;
  }
}

const char *Sensing_TemperatureText(void)
{
  return temperature_text;
}

const char *Sensing_PhText(void)
{
  return ph_text;
}
