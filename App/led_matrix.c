/**
  ******************************************************************************
  * @file    led_matrix.c
  * @brief   4×8 动态扫描引擎（每点 8 级亮度权重版）
  *
  *  硬件连接（来自 CubeMX 生成的 main.h）：
  *    阴极 K1~K6 → PA2~PA7      K7,K8 → PB0,PB1     （NMOS，高电平导通）
  *    阳极 H1~H4 → PB6,PB5,PB4,PB3                  （PMOS，低电平导通）
  *
  *  一个字符槽（1ms = TIM2 的 20 个 50µs 周期）：
  *    t=0        TIM1：阴极全灭 → 关旧 PMOS / 开新 PMOS → TIM2->CNT=0
  *               → 由本槽 duty 算出 8 个子时段边界 s_sub_bnd[]
  *    [0,50µs)   消隐
  *    [50,1000)  TIM2 第 1..19 步：按当前子时段选 g_submask 写阴极
  *
  *  子时段与权重的关系（SUB = WEIGHT_LEVELS = 8）：
  *    边界 s_sub_bnd[k] = duty*(k+1)/SUB
  *    权重 w 的点在前 s_sub_bnd[w-1] 个 tick 点亮 → 占空比 = w/SUB
  *    g_submask[c][k] = 权重 > k 的点的并集（由 led_matrix_publish 预计算）
  ******************************************************************************
  */
#include "main.h"
#include "app_config.h"
#include "led_matrix.h"

volatile uint8_t  g_weight[LED_CHAR_COUNT][LED_DOT_PER_CHAR];
volatile uint8_t  g_duty[LED_CHAR_COUNT];
volatile uint8_t  g_submask[LED_CHAR_COUNT][WEIGHT_LEVELS];
volatile uint32_t g_frame_cnt;
volatile uint32_t g_slot_cnt;

/* H1~H4 对应 B/N/G/U（上板确认：若字符出现顺序不对，只改这一行的排列） */
static const uint16_t s_anode_pin[LED_CHAR_COUNT] = {
    H1_Pin,     /* 0 = B */
    H2_Pin,     /* 1 = N */
    H3_Pin,     /* 2 = G */
    H4_Pin      /* 3 = U */
};
#define ANODE_ALL_MASK   (H1_Pin | H2_Pin | H3_Pin | H4_Pin)

static volatile uint8_t s_cur;                    /* 当前槽对应的字符 0..3 */
static volatile uint8_t s_step;                   /* 槽内 PWM 步序号 */
static volatile uint8_t s_sub_bnd[WEIGHT_LEVELS]; /* 本槽的子时段边界（tick） */
static volatile uint8_t s_sub_k;                  /* 当前子时段序号 */

/*---------------------------------------------------------------------------*/
/* 阴极掩码 → GPIO                                                            */
/*   bit0..5 → PA2..PA7 (L1..L6)，bit6..7 → PB0,PB1 (L7,L8)                   */
/*---------------------------------------------------------------------------*/
static inline void cathodes_write(uint8_t m)
{
    uint32_t a_set = ((uint32_t)( m        & 0x3Fu)) << 2;
    uint32_t a_rst = ((uint32_t)((~(uint32_t)m) & 0x3Fu)) << 18;
    uint32_t b_set = ((uint32_t)((m >> 6) & 0x03u));
    uint32_t b_rst = ((uint32_t)((~(uint32_t)(m >> 6)) & 0x03u)) << 16;

    GPIOA->BSRR = a_set | a_rst;
    GPIOB->BSRR = b_set | b_rst;
}

/*---------------------------------------------------------------------------*/
/* TIM1 @1kHz：字符槽切换                                                     */
/*---------------------------------------------------------------------------*/
void led_matrix_slot_isr(void)
{
    uint8_t  nxt    = (uint8_t)((s_cur + 1u) & 3u);
    uint32_t on_bit = s_anode_pin[nxt];
    uint8_t  d;
    uint8_t  k;

    /* ① 阴极 K1~K8 全部拉低 —— 立刻断流，LED 全灭 */
    cathodes_write(0u);

    /* ② 另外 3 个 PMOS 置高（关断），本槽的 PMOS 拉低（导通），一次写完 */
    GPIOB->BSRR = (uint32_t)(ANODE_ALL_MASK & ~on_bit) | (on_bit << 16);

    /* ③ 记录本槽字符，并由它的 duty 算出 8 个子时段边界 */
    s_cur = nxt;
    d = g_duty[nxt];
    for (k = 0u; k < WEIGHT_LEVELS; k++) {
        s_sub_bnd[k] = (uint8_t)(((uint16_t)d * (uint16_t)(k + 1u)) / WEIGHT_LEVELS);
    }
    s_sub_k = 0u;

    /* ④ 相位锁定：本槽正好含 20 个完整 PWM 周期 */
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_UPDATE);
    s_step = 0u;

    /* ⑤ 计数 */
    g_slot_cnt++;
    if (nxt == 0u) {
        g_frame_cnt++;    /* 4 槽 = 1 帧 = 4ms → 250Hz */
    }
}

/*---------------------------------------------------------------------------*/
/* TIM2 @20kHz：阴极软件 PWM                                                  */
/*---------------------------------------------------------------------------*/
void led_matrix_pwm_isr(void)
{
    uint8_t w = (uint8_t)(s_step + 1u);   /* 1 .. 19 */

    s_step = w;

    if (w < BLANK_TICKS) {                /* 消隐段：阴极保持全灭 */
        cathodes_write(0u);
        return;
    }
    w = (uint8_t)(w - (BLANK_TICKS - 1u));  /* 归一化到 1..DUTY_MAX */

    if (w > g_duty[s_cur]) {              /* 占空比窗口结束：全部熄灭 */
        cathodes_write(0u);
        return;
    }

    /* 推进子时段。w 每次只 +1，所以这里绝大多数情况只走 0~1 步 */
    while ((s_sub_k < (WEIGHT_LEVELS - 1u)) && (w > s_sub_bnd[s_sub_k])) {
        s_sub_k++;
    }
    cathodes_write(g_submask[s_cur][s_sub_k]);
}

/*---------------------------------------------------------------------------*/
/* 提交一帧：由权重推导子时段掩码，再一次性发布                                */
/*---------------------------------------------------------------------------*/
void led_matrix_publish(const uint8_t weight[LED_CHAR_COUNT][LED_DOT_PER_CHAR])
{
    uint8_t wt[LED_CHAR_COUNT][LED_DOT_PER_CHAR];
    uint8_t sub[LED_CHAR_COUNT][WEIGHT_LEVELS];
    uint8_t c, i, w;

    for (c = 0u; c < LED_CHAR_COUNT; c++) {
        uint8_t byw[WEIGHT_LEVELS + 1u];
        uint8_t acc = 0u;

        for (w = 0u; w <= WEIGHT_LEVELS; w++) {
            byw[w] = 0u;
        }
        for (i = 0u; i < LED_DOT_PER_CHAR; i++) {
            uint8_t v = weight[c][i];
            if (v > WEIGHT_LEVELS) {
                v = WEIGHT_LEVELS;
            }
            wt[c][i] = v;
            byw[v] |= (uint8_t)(1u << i);      /* 按权重分桶 */
        }
        /* 从高权重往下累积：sub[w-1] = 权重 >= w 的点的并集 */
        for (w = WEIGHT_LEVELS; w >= 1u; w--) {
            acc |= byw[w];
            sub[c][w - 1u] = acc;
        }
    }

    __disable_irq();
    for (c = 0u; c < LED_CHAR_COUNT; c++) {
        for (i = 0u; i < LED_DOT_PER_CHAR; i++) {
            g_weight[c][i] = wt[c][i];
        }
        for (w = 0u; w < WEIGHT_LEVELS; w++) {
            g_submask[c][w] = sub[c][w];
        }
    }
    __enable_irq();
}

/*---------------------------------------------------------------------------*/
void led_matrix_init(void)
{
    uint8_t c, i, w;

    for (c = 0u; c < LED_CHAR_COUNT; c++) {
        for (i = 0u; i < LED_DOT_PER_CHAR; i++) {
            g_weight[c][i] = 0u;
        }
        for (w = 0u; w < WEIGHT_LEVELS; w++) {
            g_submask[c][w] = 0u;
        }
        g_duty[c] = DUTY_DEFAULT;
    }
    for (w = 0u; w < WEIGHT_LEVELS; w++) {
        s_sub_bnd[w] = 0u;
    }
    s_sub_k = 0u;
    s_cur   = 3u;      /* 第一次 TIM1 中断会切到 0 = B */
    s_step  = 0u;
    g_frame_cnt = 0u;
    g_slot_cnt  = 0u;

    /* 保证进入主循环前 12 个栅极都是安全态 */
    cathodes_write(0u);
    GPIOB->BSRR = ANODE_ALL_MASK;    /* 置高 = PMOS 全关 */
}
