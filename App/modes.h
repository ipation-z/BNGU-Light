/**
  ******************************************************************************
  * @file    modes.h
  * @brief   各显示模式的实现接口
  *
  *  每个模式两个函数：
  *    init()  复位内部状态并画出第一帧（切模式时调用）
  *    step()  每个 100Hz 应用节拍被调用一次
  *
  *  模式内部只做一件事：把想要的亮度权重交给 led_matrix_publish()。
  ******************************************************************************
  */
#ifndef __MODES_H
#define __MODES_H

#include <stdint.h>

/* MODE5 描边游走（同时也是开机动画） */
void mode_walk_init(void);
void mode_walk_step(void);
/* 置 1 = 开机动画：走速固定成 POS_PER_DOT（120ms/点）、亮度固定用满档，
   不受电位器和 KEY3 影响，保证每次上电的自检表现完全一致。 */
void mode_walk_set_boot(uint8_t boot);
/* 跑完一轮（回到 WALK 之前）置位；取走即清。由调度器决定要不要切模式 */
uint8_t mode_walk_take_done(void);

/* MODE1 累计书写 + 逆序擦除 */
void mode_write_init(void);
void mode_write_step(void);

/* MODE2 字符流水 */
void mode_flow_init(void);
void mode_flow_step(void);

/* MODE3 同步呼吸 */
void mode_breath_init(void);
void mode_breath_step(void);

/* MODE4 流水 + 呼吸：一个字母呼吸一次后换下一个 */
void mode_mix_init(void);
void mode_mix_step(void);

#endif /* __MODES_H */
