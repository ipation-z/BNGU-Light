/**
  ******************************************************************************
  * @file    app.h
  * @brief   应用层入口与全局状态
  ******************************************************************************
  */
#ifndef __APP_H
#define __APP_H

#include <stdint.h>

extern volatile uint8_t  g_mode;      /* 当前模式号（自检期间 = MODE_SELFTEST） */
extern volatile uint8_t  g_speed;     /* 速度档 */
extern volatile uint32_t g_app_tick;  /* TIM3 递增的应用节拍计数（100Hz） */

void app_init(void);   /* 初始化各模块 + 按正确顺序启动定时器 */
void app_run(void);    /* 主循环里反复调用：有节拍就处理一次 */
void app_tick(void);   /* 单个 10ms 节拍的处理体 */

#endif /* __APP_H */
