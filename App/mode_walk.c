/**
  ******************************************************************************
  * @file    mode_walk.c
  * @brief   MODE5 描边游走（同时也是开机动画）
  *
  *  阶段循环：
  *    WALK     一个连续的三角亮度彗尾滑过 32 个点（B→N→G→U），走完自然收黑
  *    FADE_IN  32 个点一起从 0 渐亮到当前亮度峰值
  *    HOLD_ON  保持
  *    FADE_OUT 32 个点一起渐灭
  *    HOLD_OFF 全灭保持
  *    → 回到 WALK
  *
  *  开机动画模式（s_once = 1）：跑完一轮的 HOLD_OFF 后就切到 MODE1。
  *
  *  彗尾为什么平滑：头部位置 s_pos 的单位是 1/POS_PER_DOT 个点，
  *  每个点亮度查一张固定三角剖面表 s_profile，于是"亮度分布"是连续滑动的，
  *  眼睛看到的是流动而不是跳格。
  *
  *  走速由全局速度参数 g_speed（tick 数/点）决定，所以电位器能调速。
  ******************************************************************************
  */
#include "main.h"
#include <stdio.h>
#include "app_config.h"
#include "app.h"
#include "led_matrix.h"
#include "display_data.h"
#include "telemetry.h"
#include "effects.h"
#include "modes.h"

typedef enum {
    ST_WALK = 0,
    ST_FADE_IN,
    ST_HOLD_ON,
    ST_FADE_OUT,
    ST_HOLD_OFF
} st_state_t;

/* 彗尾剖面表：索引 = dd + COMET_LEAD*POS_PER_DOT（dd 单位 1/POS_PER_DOT 点） */
#define PROFILE_LEN   (((COMET_LEAD + COMET_TAIL) * POS_PER_DOT) + 1u)
/* 走完全程的位置数：最后一点的头过了之后，还要等尾巴走完才算干净 */
#define WALK_END_POS  (((LED_DOT_TOTAL - 1u + COMET_TAIL) * POS_PER_DOT))

static uint8_t  s_profile[PROFILE_LEN];
static uint8_t  s_profile_ready;

static st_state_t s_state;
static uint16_t   s_pos;         /* 头部位置，单位 1/POS_PER_DOT 点 */
static uint16_t   s_acc;         /* 位置累加器：攒够 g_speed 就前进 1 个子位置 */
static uint16_t   s_cnt;
static uint8_t    s_duty;        /* 渐亮/渐灭期间的整屏亮度 */
static uint8_t    s_last_rep;    /* 上次报到哪个字符 */
static uint8_t    s_boot;        /* 1 = 开机动画：走速和亮度都固定，不跟电位器 */
static uint8_t    s_cycle_done;  /* 跑完一轮置 1，由调度器取走 */

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
            uint8_t  b   = g_order[c][i];   /* 该走序位置对应哪颗灯 */
            uint16_t dot = (uint16_t)(((uint16_t)c * LED_DOT_PER_CHAR) + i);
            int16_t  dd  = (int16_t)pos - (int16_t)(dot * POS_PER_DOT);
            int16_t  idx = (int16_t)(dd + lead);
            uint8_t  v   = 0u;

            if ((idx >= 0) && (idx < (int16_t)PROFILE_LEN)) {
                v = s_profile[idx];
            }
            wt[c][b] = v;                   /* 权重按"灯"存，所以用位号索引 */
        }
    }
    led_matrix_publish(wt);
}

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
/* 本模式每一刻真正使用的参数：
   开机动画固定住（走速 120ms/点、满亮度），保证每次上电自检表现一致；
   作为 MODE5 被手动选中时才跟随电位器 / KEY3。 */
static uint16_t walk_ticks_per_dot(void)
{
    uint8_t sp;
    if (s_boot != 0u) {
        return (uint16_t)POS_PER_DOT;         /* 12 tick = 120ms/点 */
    }
    sp = (g_speed == 0u) ? 1u : g_speed;
    return (uint16_t)sp;
}

static uint8_t walk_duty_target(void)
{
    return (s_boot != 0u) ? (uint8_t)DUTY_MAX : g_bright;
}

void mode_walk_set_boot(uint8_t boot)
{
    s_boot = (boot != 0u) ? 1u : 0u;
}

uint8_t mode_walk_take_done(void)
{
    uint8_t d = s_cycle_done;
    s_cycle_done = 0u;
    return d;
}

void mode_walk_init(void)
{
    if (s_profile_ready == 0u) {
        build_profile();
        s_profile_ready = 1u;
    }
    s_state      = ST_WALK;
    s_pos        = 0u;
    s_acc        = 0u;
    s_cnt        = 0u;
    s_last_rep   = 0u;
    s_cycle_done = 0u;
    s_duty       = walk_duty_target();
    effects_set_duty(s_duty);
    render_walk_pos(0u);
    telemetry_line("M5", (s_boot != 0u) ? "boot animation start" : "walk start");
}

void mode_walk_step(void)
{
    char msg[32];

    switch (s_state) {

    case ST_WALK:
        /* 位置推进：每 tick 攒 POS_PER_DOT，攒够 sp 就前进一个子位置。
           开机动画 sp = POS_PER_DOT = 12 → 正好 1 tick 走 1 个子位置（120ms/点），
           和 Phase A 完全一致；作为 MODE5 时才用 g_speed（电位器可调）。 */
        s_acc = (uint16_t)(s_acc + POS_PER_DOT);
        {
            uint16_t sp = walk_ticks_per_dot();
            while (s_acc >= sp) {
                s_acc = (uint16_t)(s_acc - sp);
                s_pos++;
                if (s_pos >= WALK_END_POS) {
                    break;
                }
            }
        }

        if (s_pos >= WALK_END_POS) {
            /* 尾巴已经完全走出屏幕（全黑），进入渐亮，不会有"啪"的跳变 */
            s_state = ST_FADE_IN;
            s_cnt   = 0u;
            render_all(1u);
            s_duty = 0u;
            effects_set_duty(0u);
            telemetry_line("M5", "walk done -> fade in");
        } else {
            render_walk_pos(s_pos);
            {
                uint8_t hd = (uint8_t)(s_pos / POS_PER_DOT);
                if ((hd < LED_DOT_TOTAL) && ((hd % LED_DOT_PER_CHAR) == 0u)) {
                    uint8_t ci = (uint8_t)(hd / LED_DOT_PER_CHAR);
                    if (ci != s_last_rep) {
                        s_last_rep = ci;
                        (void)snprintf(msg, sizeof msg, "walk %s", g_char_name[ci]);
                        telemetry_line("M5", msg);
                    }
                }
            }
        }
        break;

    case ST_FADE_IN:
        if (++s_cnt >= SELFTEST_FADE_TICKS) {
            s_cnt = 0u;
            if (s_duty < g_bright) {
                s_duty++;
                effects_set_duty(s_duty);
            }
            if (s_duty >= walk_duty_target()) {
                s_state = ST_HOLD_ON;
                s_cnt   = 0u;
                telemetry_line("M5", "all on");
            }
        }
        break;

    case ST_HOLD_ON:
        if (++s_cnt >= SELFTEST_HOLD_ON_TICKS) {
            s_cnt   = 0u;
            s_state = ST_FADE_OUT;
        }
        break;

    case ST_FADE_OUT:
        if (++s_cnt >= SELFTEST_FADE_TICKS) {
            s_cnt = 0u;
            if (s_duty > 0u) {
                s_duty--;
                effects_set_duty(s_duty);
            }
            if (s_duty == 0u) {
                s_state = ST_HOLD_OFF;
                s_cnt   = 0u;
                render_all(0u);
                telemetry_line("M5", "all off");
            }
        }
        break;

    case ST_HOLD_OFF:
        if (++s_cnt >= SELFTEST_HOLD_OFF_TICKS) {
            s_cnt = 0u;
            /* 一轮跑完：通知调度器（是不是开机动画、要不要切模式由它决定），
               本模式自己继续循环，这样手动选到 MODE5 时就是无限循环播放。 */
            s_cycle_done = 1u;
            s_state      = ST_WALK;
            s_pos        = 0u;
            s_acc        = 0u;
            s_last_rep   = 0u;
            s_duty       = walk_duty_target();
            effects_set_duty(s_duty);
            render_walk_pos(0u);
            telemetry_line("M5", "walk start");
        }
        break;

    default:
        s_state = ST_WALK;
        s_cnt   = 0u;
        break;
    }
}
