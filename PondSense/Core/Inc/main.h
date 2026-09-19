/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define PH_PO_ADC_Pin GPIO_PIN_0
#define PH_PO_ADC_GPIO_Port GPIOC
#define K230_TX_Pin GPIO_PIN_2
#define K230_TX_GPIO_Port GPIOA
#define K230_RX_Pin GPIO_PIN_3
#define K230_RX_GPIO_Port GPIOA
#define FEED_IN1_Pin GPIO_PIN_6
#define FEED_IN1_GPIO_Port GPIOA
#define FEED_IN2_Pin GPIO_PIN_7
#define FEED_IN2_GPIO_Port GPIOA
#define DS18B20_DQ_Pin GPIO_PIN_0
#define DS18B20_DQ_GPIO_Port GPIOB
#define ESP01S_TX_Pin GPIO_PIN_12
#define ESP01S_TX_GPIO_Port GPIOC
#define ESP01S_RX_Pin GPIO_PIN_2
#define ESP01S_RX_GPIO_Port GPIOD
#define KEY0_Pin GPIO_PIN_4
#define KEY0_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
