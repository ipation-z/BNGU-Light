/**
  ******************************************************************************
  * @file    mode_mix.c
  * @brief   MODE4 流水 + 呼吸：一个字母完成一次呼吸（亮→灭）后换下一个
  *
  *  和 MODE3 用同一条包络（App/breath.c），区别只在"谁亮"：
  *    MODE3 —— 32 个点权重全满，四个字母一起呼吸
  *    MODE4 —— 只有当前字母的 8 个点权重满档，其余全灭，
  *             所以是"这个字母在呼吸"
  *
  *  交接为什么自然：包络在相位 0（最低点）时 duty = 0，整个屏幕是黑的，
  *  而换字母正好发生在相位绕回 0 的那一刻 —— 黑屏时换，看不见接缝。
  *
  *  速度比 MODE3 快一倍（每个相位 g_speed / MODE4_SPEED_DIV 个 tick），
  *  否则 4 个字母轮流呼吸一轮要 17 秒，演示时太拖。
  ******************************************************************************
  */
#include "main.h"
#include "app_config.h"
#include "led_matrix.h"
#include "display_data.h"
#include "telemetry.h"
#include "effects.h"
#include "breath.h"
#include "modes.h"

static uint8_t  s_letter;  /* 当前正在呼吸的字母 0..3 */
static uint8_t  s_phase;
static uint8_t  s_hold;
static uint16_t s_cnt;

/* 只点亮第 l 个字母的 8 个点 */
static void show_letter(uint8_t l)
{
    uint8_t wt[LED_CHAR_COUNT][LED_DOT_PER_CHAR];
    uint8_t c, i;

    for (c = 0u; c < LED_CHAR_COUNT; c++) {
        for (i = 0u; i < LED_DOT_PER_CHAR; i++) {
            wt[c][i] = (c == l) ? (uint8_t)WEIGHT_LEVELS : 0u;
        }
    }
    led_matrix_publish(wt);
    s_letter = l;
}

static void apply_duty(void)
{
    /* 最低档固定 0：字母之间要真的黑掉，才分得清是哪个字母在呼吸 */
    effects_set_duty(breath_duty(s_phase, g_bright, 0u));
}

static uint16_t phase_ticks(void)
{
    uint16_t t = (uint16_t)(g_speed / MODE4_SPEED_DIV);
    return (t == 0u) ? 1u : t;
}

/*---------------------------------------------------------------------------*/
void mode_mix_init(void)
{
    s_phase = 0u;
    s_hold  = (uint8_t)BREATH_DARK_HOLD_PHASES;
    s_cnt   = 0u;
    show_letter(0u);
    apply_duty();
    telemetry_line("M4", "mix start B");
}

void mode_mix_step(void)
{
    if (++s_cnt >= phase_ticks()) {
        s_cnt = 0u;
        if (s_hold > 0u) {
            s_hold--;                             /* 停在极值点不动 */
        } else {
            uint8_t nxt = (uint8_t)((s_phase + 1u) % BREATH_STEPS);

            if (nxt == 0u) {
                /* 一个字母呼吸完了。此刻 duty 正好是 0（全黑），换字母看不见接缝 */
                show_letter((uint8_t)((s_letter + 1u) % LED_CHAR_COUNT));
                s_hold = (uint8_t)BREATH_DARK_HOLD_PHASES;
                telemetry_line("M4", g_char_name[s_letter]);
            } else if (nxt == (BREATH_STEPS / 2u)) {
                s_hold = (uint8_t)BREATH_BRIGHT_HOLD_PHASES;
            }
            s_phase = nxt;
        }
    }
    apply_duty();
}
