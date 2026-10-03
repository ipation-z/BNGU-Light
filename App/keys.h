/**
  ******************************************************************************
  * @file    keys.h
  * @brief   KEY2(GPIOB14) / KEY3(GPIOB15) 去抖与按键事件
  *
  *  两个按键都是"上拉输入 + 按下接地"，所以按下 = 低电平，事件取下降沿。
  *  在 100Hz 应用节拍里采样，连续 KEY_DEBOUNCE_TICKS 次电平一致才认账。
  ******************************************************************************
  */
#ifndef __KEYS_H
#define __KEYS_H

#include <stdint.h>

#define KEY_EV_NONE   0x00u
#define KEY_EV_KEY2   0x01u
#define KEY_EV_KEY3   0x02u

void    keys_init(void);
void    keys_tick(void);          /* 100Hz 调用 */
uint8_t keys_take_event(void);    /* 取走并清空事件位（KEY_EV_*） */

#endif /* __KEYS_H */
