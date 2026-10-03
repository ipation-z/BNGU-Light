/**
  ******************************************************************************
  * @file    app_config.h
  * @brief   BNGU_Light 所有可调参数的唯一来源（改参数只改这里）
  ******************************************************************************
  */
#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/* ============================ 扫描引擎 ============================ */
#define LED_CHAR_COUNT        4u      /* B / N / G / U */
#define LED_DOT_PER_CHAR      8u      /* 每个字符 8 颗灯（K1~K8） */
#define LED_DOT_TOTAL         32u     /* 4 × 8 */

/* 一个字符槽 = 1ms = TIM2 的 20 个周期（TIM2 ARR=49 → 50µs） */
#define PWM_TICKS_PER_SLOT    20u

/* 槽起点的消隐 tick 数：
 *   1 → Vgs 在 50µs 时约 -0.56V，低于 AO3401 阈值 -0.9V，一般看不到鬼影
 *   2 → 100µs，Vgs 约 -0.09V，完全关断，但亮度上限降到 18
 * 上板若看到前一个字符残影，就把它改成 2。 */
#define BLANK_TICKS           1u
#define DUTY_MAX              (PWM_TICKS_PER_SLOT - BLANK_TICKS)   /* 19 */

/* 对外默认亮度（0..DUTY_MAX）。Phase A 自检会临时用满 DUTY_MAX */
#define DUTY_DEFAULT          15u

/* 每个槽起点把 TIM2->CNT 清零做相位锁定（见 led_matrix.c） */

/* ---------------------------------------------------------------------------
 * 点的亮度权重：把 1ms 的槽再切成 WEIGHT_LEVELS 个子时段，
 * 每个点可以设置 0..WEIGHT_LEVELS 的权重，表示它在多少个连续子时段里点亮。
 * 于是同一个字符内部也能做出 8 级明暗（PWM 在共用阴极上做不到分点调光）。
 *   权重 8 = 100%，4 = 50%，1 = 12.5%，0 = 不亮
 * 全局亮度仍然由 duty 控制，两者相乘。
 * ------------------------------------------------------------------------- */
#define WEIGHT_LEVELS         8u

/* ============================ 应用节拍 ============================ */
#define APP_TICK_HZ           100u    /* TIM3 = 100Hz */

/* ============================ ADC ============================ */
#define ADC_CH_KEY            0u      /* DMA 缓冲[0] = CH0 = PA0 = 电源键检测 */
#define ADC_CH_POT            1u      /* DMA 缓冲[1] = CH1 = PA1 = 电位器   */
#define ADC_VREF_MV           3300u
#define ADC_FULL_SCALE        4095u

/* ============================ 串口遥测 ============================ */
#define TLM_PERIOD_TICKS      10u     /* 100Hz / 10 = 10Hz */
#define TLM_BUF_SIZE          128u

/* ============================ 描边游走自检 ============================ */
/* 节奏：1 tick = 10ms（来自 TIM3 的 100Hz 应用节拍）
 *
 *  SELFTEST_STEP_TICKS      亮点每走一个点的时间（决定"走得快不快"）
 *      6  →  60ms/点   走完 32 点 ≈ 1.9s   偏快，单颗灯看不清
 *      8  →  80ms/点   走完 32 点 ≈ 2.6s
 *     10  → 100ms/点   走完 32 点 ≈ 3.2s
 *     12  → 120ms/点   走完 32 点 ≈ 3.8s   ← 当前
 *     15  → 150ms/点   走完 32 点 ≈ 4.8s   偏拖沓
 *     20  → 200ms/点   走完 32 点 ≈ 6.4s
 *     25  → 250ms/点   走完 32 点 ≈ 8.0s   很慢，适合逐点检查焊接
 *
 *  SELFTEST_HOLD_ON_TICKS   全亮保持多久（演示/拍照用）
 *  SELFTEST_FADE_TICKS      渐亮 / 渐灭的步长：每多少 tick 变 1 级亮度
 *                           2 → 19 级 × 20ms ≈ 0.38s
 *  SELFTEST_HOLD_OFF_TICKS  全灭保持多久
 *
 *  平滑彗尾：亮点不是"一格一格跳"，而是一个连续的三角亮度分布滑过 32 个点。
 *    POS_PER_DOT        每个点细分成多少个位置（越大越平滑，CPU 略增）
 *    POS_ADVANCE_TICKS  每多少个 tick 前进 1 个子位置
 *    → 一个点的走行时间 = POS_PER_DOT × POS_ADVANCE_TICKS 个 tick
 *        12 × 1 = 12 tick = 120ms/点   ← 当前
 *        12 × 2 = 24 tick = 240ms/点   （慢一倍，平滑度不变）
 *        20 × 1 = 20 tick = 200ms/点   （更平滑也更慢）
 *    COMET_LEAD  头部前方渐亮跨几个点（0 = 头部直接跳到满亮，会有"啪"的跳变）
 *    COMET_TAIL  头部后方渐灭跨几个点（越大拖尾越长）
 */
#define SELFTEST_HOLD_ON_TICKS    70u   /* 0.7s 全亮 */
#define SELFTEST_FADE_TICKS        2u   /* 每 20ms 变 1 级 → 约 0.38s 渐亮/渐灭 */
#define SELFTEST_HOLD_OFF_TICKS   30u   /* 0.3s 全灭 */

#define POS_PER_DOT               12u   /* 每个点细分成 12 个位置 */
#define POS_ADVANCE_TICKS          1u   /* 每 1 个 tick 前进 1 个子位置 */
#define COMET_LEAD                 1u   /* 头部前方 1 个点的渐亮 */
#define COMET_TAIL                 3u   /* 头部后方 3 个点的渐灭 */

/* ============================ 应用状态默认值 ============================ */
#define MODE_SELFTEST         0u
#define SPEED_DEFAULT         30u

#endif /* __APP_CONFIG_H */
