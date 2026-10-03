/**
  ******************************************************************************
  * @file    breath.h
  * @brief   呼吸包络（MODE3 同步呼吸 / MODE4 流水+呼吸 共用）
  *
  *  包络是一条 64 相位的半正弦：
  *      env[i] = round(255 · sin(π·i/64))      i = 0 .. BREATH_STEPS-1
  *  相位 0 是最低点（duty 最低），相位 BREATH_STEPS/2 是最高点，然后对称回落。
  *
  *  为什么用半正弦而不是升余弦：
  *    升余弦在两端是平的（i 很小时 u ≈ i²，几乎不动），再取平方更平。
  *    而 duty 的级数只有 20 级（TIM2 20kHz，1ms 槽 20 个 tick），底部一整段
  *    会被 round 成 duty 0 —— 实测约 1 秒停在"全灭"然后突然跳到 duty 1，
  *    看起来就是"熄灭时卡一下"。半正弦在 i=0 处斜率最大、一点都不平，
  *    每个相位都在动，不会有这种卡顿。
  ******************************************************************************
  */
#ifndef __BREATH_H
#define __BREATH_H

#include <stdint.h>
#include "app_config.h"

/* phase: 0..BREATH_STEPS-1；peak: 亮度峰值；dmin: 最低亮度档。
   返回 duty ∈ [dmin, peak]（peak <= dmin 时直接返回 peak）。 */
uint8_t breath_duty(uint8_t phase, uint8_t peak, uint8_t dmin);

#endif /* __BREATH_H */
