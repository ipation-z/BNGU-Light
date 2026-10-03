/**
  ******************************************************************************
  * @file    led_matrix.h
  * @brief   4×8 动态扫描引擎：TIM1 负责槽切换，TIM2 负责阴极软件 PWM
  *
  *  显示数据 = 每字符 8 个点的「亮度权重」+ 每字符一个「整体亮度 duty」
  *
  *    g_weight[字符][点]   0..WEIGHT_LEVELS（8 = 100%，4 = 50%，0 = 不亮）
  *    g_duty[字符]         0..DUTY_MAX，整字符的整体亮度
  *
  *  为什么要有权重：PWM 做在共用阴极上，一个槽里只有一个字符导通，所以
  *  同一个字符内的点本来只能同亮度。为此把 1ms 的槽再切成 WEIGHT_LEVELS
  *  个子时段，权重 w 的点在前 w 个子时段点亮 —— 于是同一字符内也能有
  *  8 级明暗，可以做平滑的彗尾、渐变、拖尾。
  *
  *  子时段掩码由权重自动推导，效果层只要填权重就行。
  ******************************************************************************
  */
#ifndef __LED_MATRIX_H
#define __LED_MATRIX_H

#include <stdint.h>
#include "app_config.h"

/* 每字符 8 个点的亮度权重（效果层写） */
extern volatile uint8_t g_weight[LED_CHAR_COUNT][LED_DOT_PER_CHAR];
/* 每字符的整体亮度 0..DUTY_MAX */
extern volatile uint8_t g_duty[LED_CHAR_COUNT];
/* 由权重推导出的子时段掩码：g_submask[c][k] = 权重 > k 的点（扫描引擎内部用） */
extern volatile uint8_t g_submask[LED_CHAR_COUNT][WEIGHT_LEVELS];

extern volatile uint32_t g_frame_cnt;   /* 250Hz 帧计数（4 槽 = 1 帧 = 4ms） */
extern volatile uint32_t g_slot_cnt;    /* 1kHz 槽计数 */

void led_matrix_init(void);

/* 立即让 12 个栅极进入安全态（阴极全低 + 4 个 PMOS 全关断）。
   用于关机时序：先停掉 TIM1/TIM2，再调它，避免中断把 GPIO 又改回去。 */
void led_matrix_off(void);

/* 提交一帧权重。内部会推导子时段掩码，并关中断 <2µs 一次性发布，
   保证四个字符不会出现"半新半旧"。 */
void led_matrix_publish(const uint8_t weight[LED_CHAR_COUNT][LED_DOT_PER_CHAR]);

/* 中断服务体，由 HAL_TIM_PeriodElapsedCallback 分派（见 app.c） */
void led_matrix_slot_isr(void);   /* TIM1 @1kHz */
void led_matrix_pwm_isr(void);    /* TIM2 @20kHz */

#endif /* __LED_MATRIX_H */
