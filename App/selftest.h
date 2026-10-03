/**
  ******************************************************************************
  * @file    selftest.h
  * @brief   描边游走自检动画（不是模式，Phase A 的检测程序）
  ******************************************************************************
  */
#ifndef __SELFTEST_H
#define __SELFTEST_H

void selftest_init(void);
void selftest_tick(void);   /* 由 100Hz 应用节拍调用 */

#endif /* __SELFTEST_H */
