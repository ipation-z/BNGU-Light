/**
  ******************************************************************************
  * @file    mode_flow.c
  * @brief   MODE2 字符流水
  *
  *  一个字母的 8 颗灯全亮 → 保持一段时间 → 换下一个字母全亮 → 循环。
  *  字母的形状来自 8 颗灯的空间排布，所以"全亮"就是画出这个字母。
  *
  *  速度：每个字母保持 g_speed × MODE2_SPEED_MUL 个 100Hz 节拍。
  ******************************************************************************
  */
#include "main.h"
#include "app_config.h"
#include "led_matrix.h"
#include "display_data.h"
#include "telemetry.h"
#include "effects.h"
#include "modes.h"

static uint8_t  s_cur;      /* 当前字母 0..3 */
static uint16_t s_cnt;

static void render_letter(uint8_t c)
{
    uint8_t wt[LED_CHAR_COUNT][LED_DOT_PER_CHAR];
    uint8_t cc, i;

    for (cc = 0u; cc < LED_CHAR_COUNT; cc++) {
        for (i = 0u; i < LED_DOT_PER_CHAR; i++) {
            wt[cc][i] = (cc == c) ? (uint8_t)WEIGHT_LEVELS : 0u;
        }
    }
    led_matrix_publish(wt);
}

/*---------------------------------------------------------------------------*/
void mode_flow_init(void)
{
    s_cur = 0u;
    s_cnt = 0u;
    render_letter(0u);
    telemetry_line("M2", "flow start");
}

void mode_flow_step(void)
{
    uint16_t t = (uint16_t)g_speed * MODE2_SPEED_MUL;

    if (t == 0u) {
        t = 1u;
    }
    if (++s_cnt < t) {
        return;
    }
    s_cnt = 0u;
    s_cur = (uint8_t)((s_cur + 1u) % LED_CHAR_COUNT);
    render_letter(s_cur);
    telemetry_line("M2", g_char_name[s_cur]);
}
