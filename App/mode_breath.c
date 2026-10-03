/**
  ******************************************************************************
  * @file    mode_breath.c
  * @brief   MODE3 同步呼吸：四个字母同时由暗到亮、再由亮到暗
  *
  *  32 颗灯权重全满，只调每个字符的整体亮度 duty —— 四个字符的 duty 一样，
  *  所以是"同时呼吸"。包络本身在 App/breath.c（半正弦，MODE3/MODE4 共用）。
  *
  *  最低点 duty = MODE3_DUTY_MIN（默认 0，真的断电），
  *  最高点 duty = g_bright（电位器的亮度峰值），
  *  所以电位器直接缩放整条包络。
  *
  *  速度：每个相位持续 g_speed / BREATH_SPEED_DIV 个 tick。
  *  最高点、最低点各停 BREATH_*_HOLD_PHASES 个相位（"吸-停-呼-停"）。
  ******************************************************************************
  */
#include "main.h"
#include "app_config.h"
#include "led_matrix.h"
#include "telemetry.h"
#include "effects.h"
#include "breath.h"
#include "modes.h"

static uint8_t  s_phase;   /* 0..BREATH_STEPS-1 */
static uint8_t  s_hold;    /* 在极值点还要停住几个相位 */
static uint16_t s_cnt;

static void apply_duty(void)
{
    effects_set_duty(breath_duty(s_phase, g_bright, (uint8_t)MODE3_DUTY_MIN));
}

static uint16_t phase_ticks(void)
{
    uint16_t t = (uint16_t)(g_speed / BREATH_SPEED_DIV);
    return (t == 0u) ? 1u : t;
}

/*---------------------------------------------------------------------------*/
void mode_breath_init(void)
{
    uint8_t wt[LED_CHAR_COUNT][LED_DOT_PER_CHAR];
    uint8_t c, i;

    /* 32 个点全部权重满档 —— 呼吸只靠 duty，不靠权重 */
    for (c = 0u; c < LED_CHAR_COUNT; c++) {
        for (i = 0u; i < LED_DOT_PER_CHAR; i++) {
            wt[c][i] = (uint8_t)WEIGHT_LEVELS;
        }
    }
    led_matrix_publish(wt);

    s_phase = 0u;
    s_hold  = (uint8_t)BREATH_DARK_HOLD_PHASES;   /* 上电从"全灭"开始，也停一下 */
    s_cnt   = 0u;
    apply_duty();
    telemetry_line("M3", "breath start");
}

void mode_breath_step(void)
{
    if (++s_cnt >= phase_ticks()) {
        s_cnt = 0u;
        if (s_hold > 0u) {
            s_hold--;                             /* 停在极值点不动 */
        } else {
            s_phase = (uint8_t)((s_phase + 1u) % BREATH_STEPS);
            if (s_phase == 0u) {
                s_hold = (uint8_t)BREATH_DARK_HOLD_PHASES;    /* 最低点：灭着停 */
            } else if (s_phase == (BREATH_STEPS / 2u)) {
                s_hold = (uint8_t)BREATH_BRIGHT_HOLD_PHASES;  /* 最高点：亮着停 */
            }
        }
    }
    /* 每 tick 都要重设 duty：调度器在调用 step() 之前刚把它设成了亮度峰值 */
    apply_duty();
}
