/**
  ******************************************************************************
  * @file    selftest.c
  * @brief   描边游走自检动画（平滑彗尾版）
  *
  *  阶段：
  *    WALK    一个连续的三角亮度彗尾滑过 32 个点（B→N→G→U），走完自然收黑
  *    FADE_IN 32 个点一起从 0 渐亮到满
  *    HOLD_ON 全亮保持
  *    FADE_OUT 32 个点一起渐灭
  *    HOLD_OFF 全灭保持
  *    → 回到 WALK（Phase A 循环；Phase B 起改为上电只跑一次）
  *
  *  为什么彗尾是"平滑"的：
  *    头部位置 s_pos 的单位是 1/POS_PER_DOT 个点，每个 tick 前进一次，
  *    所以头部在一颗灯上停留的时间内会推进 POS_PER_DOT 个子位置。
  *    每个点的亮度 = 查一张固定的三角剖面表 s_profile，
  *    于是"亮度分布"是连续滑动的，眼睛看到的是流动而不是跳格。
  *
  *  K 索引重排表：因为一个点的空间位置对每个字符是独立的，走序和 K 编号
  *  的对应关系可以逐字符配置 —— 见 s_order[][]。
  ******************************************************************************
  */
#include "main.h"
#include <stdio.h>
#include "app_config.h"
#include "app.h"
#include "led_matrix.h"
#include "telemetry.h"
#include "selftest.h"

typedef enum {
    ST_WALK = 0,
    ST_FADE_IN,
    ST_HOLD_ON,
    ST_FADE_OUT,
    ST_HOLD_OFF
} st_state_t;

/* K 索引重排表（每个字符一张）：
 *   s_order[字符][走序位置] = 该位置要点亮的位号（0 = K1，7 = K8）
 * 默认全部是按 K1→K8 的走序。上板后发现某个字符的笔画顺序不对，
 * 只改对应那一行，不用动硬件、也不用改逻辑。
 * 注意：走序位置是 0 基 —— 第 6 步是下标 5，第 8 步是下标 7。 */
static const uint8_t s_order[LED_CHAR_COUNT][LED_DOT_PER_CHAR] = {
    /* B */ {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u},
    /* N */ {0u, 1u, 2u, 3u, 4u, 7u, 6u, 5u},   /* 第 6 灯与第 8 灯互换 */
    /* G */ {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u},
    /* U */ {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u}
};

static const char *const s_char_name[LED_CHAR_COUNT] = {"B", "N", "G", "U"};

/* 彗尾剖面表：索引 = dd + COMET_LEAD*POS_PER_DOT（dd 单位 1/POS_PER_DOT 点） */
#define PROFILE_LEN   (((COMET_LEAD + COMET_TAIL) * POS_PER_DOT) + 1u)
static uint8_t s_profile[PROFILE_LEN];

/* 走完全程的位置数：最后一点的头过了之后，还要等尾巴走完才算干净 */
#define WALK_END_POS  (((LED_DOT_TOTAL - 1u + COMET_TAIL) * POS_PER_DOT))

static st_state_t s_state;
static uint16_t   s_pos;         /* 头部位置，单位 1/POS_PER_DOT 点 */
static uint16_t   s_pos_cnt;     /* 位置推进的 tick 计数 */
static uint16_t   s_cnt;         /* 各阶段的保持计数 */
static uint8_t    s_duty;        /* 渐亮/渐灭时用的全局亮度 */
static uint8_t    s_last_rep;    /* 上一次报告到哪个字符 */

/*---------------------------------------------------------------------------*/
static void build_profile(void)
{
    const uint16_t lead_span = (uint16_t)(COMET_LEAD * POS_PER_DOT);
    const uint16_t tail_span = (uint16_t)(COMET_TAIL * POS_PER_DOT);
    uint16_t i;

    for (i = 0u; i < PROFILE_LEN; i++) {
        int16_t dd = (int16_t)i - (int16_t)lead_span;
        uint8_t v;

        if (dd >= 0) {
            /* 头部后方：从满亮线性衰减到 0 */
            v = ((uint16_t)dd >= tail_span)
              ? 0u
              : (uint8_t)((((uint16_t)(tail_span - (uint16_t)dd)) * WEIGHT_LEVELS)
                          / tail_span);
        } else {
            /* 头部前方：从 0 线性升到满亮（避免头部"啪"地跳出来） */
            v = (uint8_t)((((uint16_t)(dd + (int16_t)lead_span)) * WEIGHT_LEVELS)
                          / lead_span);
        }
        s_profile[i] = v;
    }
}

/* 全局亮度（渐亮/渐灭用）：一次改 4 个字符的 duty */
static void set_all_duty(uint8_t d)
{
    uint8_t i;
    __disable_irq();
    for (i = 0u; i < LED_CHAR_COUNT; i++) {
        g_duty[i] = d;
    }
    __enable_irq();
}

/* 把"头部位置"翻译成 32 个点的亮度权重 */
static void render_walk_pos(uint16_t pos)
{
    uint8_t wt[LED_CHAR_COUNT][LED_DOT_PER_CHAR];
    uint8_t  c, i;
    int16_t  lead = (int16_t)(COMET_LEAD * POS_PER_DOT);

    for (c = 0u; c < LED_CHAR_COUNT; c++) {
        for (i = 0u; i < LED_DOT_PER_CHAR; i++) {
            wt[c][i] = 0u;
        }
    }

    for (c = 0u; c < LED_CHAR_COUNT; c++) {
        for (i = 0u; i < LED_DOT_PER_CHAR; i++) {
            uint8_t  b   = s_order[c][i];   /* 该走序位置对应哪颗灯 */
            uint16_t dot = (uint16_t)(((uint16_t)c * LED_DOT_PER_CHAR) + i);
            int16_t  dd  = (int16_t)pos - (int16_t)(dot * POS_PER_DOT);
            int16_t  idx = (int16_t)(dd + lead);
            uint8_t  v   = 0u;

            if ((idx >= 0) && (idx < (int16_t)PROFILE_LEN)) {
                v = s_profile[idx];
            }
            wt[c][b] = v;                   /* 权重是按"灯"存的，所以用位号索引 */
        }
    }
    led_matrix_publish(wt);
}

/* 全亮 / 全灭：所有点权重一起给 */
static void render_all(uint8_t on)
{
    uint8_t wt[LED_CHAR_COUNT][LED_DOT_PER_CHAR];
    uint8_t c, i;

    for (c = 0u; c < LED_CHAR_COUNT; c++) {
        for (i = 0u; i < LED_DOT_PER_CHAR; i++) {
            wt[c][i] = (on != 0u) ? (uint8_t)WEIGHT_LEVELS : 0u;
        }
    }
    led_matrix_publish(wt);
}

/*---------------------------------------------------------------------------*/
void selftest_init(void)
{
    build_profile();

    /* 自检用满亮度，暗灯/虚焊一眼可见 */
    s_duty = DUTY_MAX;
    set_all_duty(s_duty);

    s_state    = ST_WALK;
    s_pos      = 0u;
    s_pos_cnt  = 0u;
    s_cnt      = 0u;
    s_last_rep = 0u;
    render_walk_pos(0u);
    telemetry_line("TEST", "WALK start  B 1/8");
}

void selftest_tick(void)
{
    char msg[32];

    switch (s_state) {

    /* 彗尾滑过 32 个点。头部位置每个 tick 前进一次，
       走完一个点需要 POS_PER_DOT × POS_ADVANCE_TICKS 个 tick。 */
    case ST_WALK:
        if (++s_pos_cnt >= POS_ADVANCE_TICKS) {
            s_pos_cnt = 0u;
            s_pos++;

            if (s_pos >= WALK_END_POS) {
                /* 尾巴已经完全走出屏幕（全黑），直接进入渐亮，不会有"啪"的跳变 */
                s_state = ST_FADE_IN;
                s_cnt   = 0u;
                render_all(1u);
                s_duty = 0u;
                set_all_duty(s_duty);
                telemetry_line("TEST", "WALK done -> FADE IN");
            } else {
                render_walk_pos(s_pos);

                /* 头部每进入一个新字符就报一条进度 */
                {
                    uint8_t hd = (uint8_t)(s_pos / POS_PER_DOT);
                    if ((hd < LED_DOT_TOTAL) &&
                        ((hd % LED_DOT_PER_CHAR) == 0u)) {
                        uint8_t ci = (uint8_t)(hd / LED_DOT_PER_CHAR);
                        if (ci != s_last_rep) {
                            s_last_rep = ci;
                            (void)snprintf(msg, sizeof msg, "WALK %s 1/8",
                                           s_char_name[ci]);
                            telemetry_line("TEST", msg);
                        }
                    }
                }
            }
        }
        break;

    /* 32 个点一起渐亮：每 SELFTEST_FADE_TICKS 升 1 级 duty */
    case ST_FADE_IN:
        if (++s_cnt >= SELFTEST_FADE_TICKS) {
            s_cnt = 0u;
            if (s_duty < DUTY_MAX) {
                s_duty++;
                set_all_duty(s_duty);
            }
            if (s_duty >= DUTY_MAX) {
                s_state = ST_HOLD_ON;
                s_cnt   = 0u;
                telemetry_line("TEST", "ALL ON");
            }
        }
        break;

    case ST_HOLD_ON:
        if (++s_cnt >= SELFTEST_HOLD_ON_TICKS) {
            s_cnt   = 0u;
            s_state = ST_FADE_OUT;
            telemetry_line("TEST", "FADE OUT");
        }
        break;

    /* 32 个点一起渐灭 */
    case ST_FADE_OUT:
        if (++s_cnt >= SELFTEST_FADE_TICKS) {
            s_cnt = 0u;
            if (s_duty > 0u) {
                s_duty--;
                set_all_duty(s_duty);
            }
            if (s_duty == 0u) {
                s_state = ST_HOLD_OFF;
                s_cnt   = 0u;
                render_all(0u);
                telemetry_line("TEST", "ALL OFF");
            }
        }
        break;

    case ST_HOLD_OFF:
        if (++s_cnt >= SELFTEST_HOLD_OFF_TICKS) {
            s_cnt      = 0u;
            s_state    = ST_WALK;
            s_pos      = 0u;
            s_pos_cnt  = 0u;
            s_last_rep = 0u;
            s_duty     = DUTY_MAX;
            set_all_duty(s_duty);
            render_walk_pos(0u);
            telemetry_line("TEST", "WALK start  B 1/8");
        }
        break;

    default:
        s_state = ST_WALK;
        s_cnt   = 0u;
        break;
    }
}
