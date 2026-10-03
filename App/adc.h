/**
  ******************************************************************************
  * @file    adc.h
  * @brief   ADC 采集（TIM3 TRGO 触发 + DMA 循环搬运）
  *
  *  缓冲顺序由 CubeMX 的 Rank 决定：
  *    s_raw[0] = ADC_CH_KEY = CH0 = PA0 = 电源键检测分压
  *    s_raw[1] = ADC_CH_POT = CH1 = PA1 = 电位器
  *
  *  注意：TIM3 的 update 事件同时产生 TRGO（启动 ADC）和 TIM3 中断，
  *  而一次扫描要 27µs 才结束，所以主循环读到的是“上一轮”的值（滞后 10ms），
  *  对按键和电位器完全够用，而且天然不会读到半更新的数据。
  ******************************************************************************
  */
#ifndef __ADC_APP_H
#define __ADC_APP_H

#include <stdint.h>

void     adc_init(void);          /* 校准 + 启动 DMA */
void     adc_update(void);        /* 由 100Hz 应用节拍调用：换算 mV + 轻滤波 */
uint16_t adc_mv(uint8_t ch);      /* 换算后的电压（mV），4 点低通 */
uint16_t adc_raw(uint8_t ch);     /* 最近一次的原始码值 0..4095 */

#endif /* __ADC_APP_H */
