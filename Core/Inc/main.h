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
#include "stm32f1xx_hal.h"

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
/* CubeMX 在 main.c 中定义的外设句柄，供 App/ 层引用。
   放在 USER CODE 段内，重新生成代码时不会被覆盖。 */
extern ADC_HandleTypeDef  hadc1;
extern TIM_HandleTypeDef  htim1;
extern TIM_HandleTypeDef  htim2;
extern TIM_HandleTypeDef  htim3;
extern UART_HandleTypeDef huart1;
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define L1_Pin GPIO_PIN_2
#define L1_GPIO_Port GPIOA
#define L2_Pin GPIO_PIN_3
#define L2_GPIO_Port GPIOA
#define L3_Pin GPIO_PIN_4
#define L3_GPIO_Port GPIOA
#define L4_Pin GPIO_PIN_5
#define L4_GPIO_Port GPIOA
#define L5_Pin GPIO_PIN_6
#define L5_GPIO_Port GPIOA
#define L6_Pin GPIO_PIN_7
#define L6_GPIO_Port GPIOA
#define L7_Pin GPIO_PIN_0
#define L7_GPIO_Port GPIOB
#define L8_Pin GPIO_PIN_1
#define L8_GPIO_Port GPIOB
#define KEY1_Pin GPIO_PIN_11
#define KEY1_GPIO_Port GPIOB
#define KEY2_Pin GPIO_PIN_14
#define KEY2_GPIO_Port GPIOB
#define KEY3_Pin GPIO_PIN_15
#define KEY3_GPIO_Port GPIOB
#define H4_Pin GPIO_PIN_3
#define H4_GPIO_Port GPIOB
#define H3_Pin GPIO_PIN_4
#define H3_GPIO_Port GPIOB
#define H2_Pin GPIO_PIN_5
#define H2_GPIO_Port GPIOB
#define H1_Pin GPIO_PIN_6
#define H1_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
