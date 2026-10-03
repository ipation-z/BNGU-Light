/**
  ******************************************************************************
  * @file    display_data.h
  * @brief   显示布局数据：K 索引与走序的对应关系
  ******************************************************************************
  */
#ifndef __DISPLAY_DATA_H
#define __DISPLAY_DATA_H

#include <stdint.h>
#include "app_config.h"

/* K 索引重排表（每个字符一张）：
 *   g_order[字符][走序位置] = 该位置要点亮的位号（0 = K1，7 = K8）
 *
 * 为什么每个字符可以不一样：一个点的空间位置对四个字符是各自独立的，
 * 所以"走序"和"K 编号"的对应关系可以逐字符配置，改这一张表就能调整
 * 笔画顺序，不用动硬件。
 *
 * 注意下标是 0 基：第 6 步是下标 5，第 8 步是下标 7。 */
extern const uint8_t g_order[LED_CHAR_COUNT][LED_DOT_PER_CHAR];

/* 字符名，遥测和调试用 */
extern const char *const g_char_name[LED_CHAR_COUNT];

#endif /* __DISPLAY_DATA_H */
