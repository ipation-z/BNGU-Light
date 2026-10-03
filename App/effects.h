/**
  ******************************************************************************
  * @file    effects.h
  * @brief   模式调度：模式表、切换（含闪灯提示）、亮度/速度参数、电位器解析
  ******************************************************************************
  */
#ifndef __EFFECTS_H
#define __EFFECTS_H

#include <stdint.h>
#include "app_config.h"

/* ---- 全局可调参数（由电位器 + KEY3 决定）---- */
extern volatile uint8_t g_bright;      /* 亮度峰值 0..DUTY_MAX */
extern volatile uint8_t g_speed;       /* 每多少 tick 推进一步（越小越快） */
extern volatile uint8_t g_pot_target;  /* POT_TARGET_* */

/* ---- 给遥测用的可读名字 ---- */
const char *effects_mode_name(void);
const char *effects_pot_name(void);

void effects_init(void);                        /* 初始化 + 进入开机动画 */
void effects_tick(void);                        /* 100Hz 节拍 */
void effects_set_duty(uint8_t d);               /* 覆盖整屏亮度（模式内部用，比如渐灭） */
void effects_request_mode(uint8_t m);           /* 带闪灯提示的切换（KEY2 用） */
void effects_set_mode_now(uint8_t m);           /* 立即切换、不闪（开机动画结束用） */

/* ---- 串口 / 蓝牙控制（App/cli.c 用）---- */
/* 待机（黑屏等按键）时被串口指令叫醒 → 进 MODE1。三个设置函数内部会自动调。 */
void    effects_wake(void);

/* 按百分比设置：走和电位器完全相同的映射，设完冻结软接管，
   所以值会稳定生效，直到用户真的转动电位器才交回旋钮。 */
void    effects_set_bright_pct(uint8_t pct);    /* 0..100 */
void    effects_set_speed_pct(uint8_t pct);     /* 0..100 */

/* 反算成百分比，用于 ASK 回读 */
uint8_t effects_bright_pct(void);
uint8_t effects_speed_pct(void);

/* 1 = 电位器正在跟手（手动调节中）；0 = 参数冻结（蓝牙设的值保持中） */
uint8_t effects_pot_is_linked(void);

#endif /* __EFFECTS_H */
