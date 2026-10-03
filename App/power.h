/**
  ******************************************************************************
  * @file    power.h
  * @brief   一键开关机（Phase E）
  ******************************************************************************
  */
#ifndef __POWER_H
#define __POWER_H

#include <stdint.h>

void power_init(void);

/* 100Hz 应用节拍调用。检测到"再按一下"就直接执行关机时序，永不返回。 */
void power_tick(void);

/* 1 = 已经进入关机流程（app_tick 应该立刻停止其它工作） */
uint8_t power_is_off(void);

/* 给遥测用的状态名 */
const char *power_state_name(void);

#endif /* __POWER_H */
