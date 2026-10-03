/**
  ******************************************************************************
  * @file    mode_breath.c
  * @brief   MODE3 同步呼吸：四个字母同时由暗到亮、再由亮到暗
  *
  *  32 颗灯权重全满，只调每个字符的整体亮度 duty —— 四个字符的 duty 一样，
  *  所以是"同时呼吸"。
  *
  *  亮度包络用一张 64 相位的表（s_env，0..255），形状是"半正弦"：
  *      env[i] = sin(π·i/64) × 255
  *
  *  为什么用半正弦，而不是以前那个"升余弦平方"：
  *    升余弦在两端是平的（i 很小时 u ≈ i²，几乎不动），再平方就更平。
  *    而 duty 的级数只有 20 级（TIM2 20kHz，1ms 槽 20 个 tick），底部那一大段
  *    会被 round 成 duty 0 —— 实测有 16/64 个相位（约 1 秒！）停在"全灭"，
  *    然后一下子跳到 duty 1（感知亮度 38%），看起来就是"熄灭时卡一下"。
  *    半正弦在 i=0 处斜率最大、一点都不平，所以每一个相位都在动，
  *    不会再有一大段浪费在 duty 0。
  *
  *  另外最低档不取 0 而取 MODE3_DUTY_MIN（默认 1）：
  *    duty 0→1 本身在感知上就是 0→38% 的突跳（LED 在 1/19 占空比时就挺亮了），
  *    避开这一步，"快熄灭"那一段才顺。duty 1 时每颗灯平均只有 0.15mA，
  *    看起来已经和熄灭没区别。
  *
  *  实际 duty = MODE3_DUTY_MIN + (g_bright - MODE3_DUTY_MIN) × env / 255，
  *  所以电位器的"亮度峰值"直接缩放整条包络。
  *
  *  速度：每个相位持续 g_speed / MODE3_SPEED_DIV 个 tick
  *  （64 个相位比 32 个点细一倍，所以除 2，整体周期才和别的模式手感一致）。
  ******************************************************************************
  */
#include "main.h"
#include "app_config.h"
#include "led_matrix.h"
#include "telemetry.h"
#include "effects.h"
#include "modes.h"

/* 半正弦，归一化到 0..255：env[i] = round(255 · sin(π·i/64)) */
static const uint8_t s_env[BREATH_STEPS] = {
      0u,  13u,  25u,  37u,  50u,  62u,  74u,  86u,
     98u, 109u, 120u, 131u, 142u, 152u, 162u, 171u,
    180u, 189u, 197u, 205u, 212u, 219u, 225u, 231u,
    236u, 240u, 244u, 247u, 250u, 252u, 254u, 255u,
    255u, 255u, 254u, 252u, 250u, 247u, 244u, 240u,
    236u, 231u, 225u, 219u, 212u, 205u, 197u, 189u,
    180u, 171u, 162u, 152u, 142u, 131u, 120u, 109u,
     98u,  86u,  74u,  62u,  50u,  37u,  25u,  13u
};

static uint8_t  s_phase;   /* 0..BREATH_STEPS-1 */
static uint8_t  s_hold;    /* 到了最低点后还要停住几个相位 */
static uint16_t s_cnt;

static void apply_duty(void)
{
    uint8_t d;

    if (g_bright <= (uint8_t)MODE3_DUTY_MIN) {
        d = g_bright;                       /* 亮度峰值比最低档还低，就听峰值的 */
    } else {
        uint16_t span = (uint16_t)(g_bright - (uint8_t)MODE3_DUTY_MIN);
        uint16_t x    = ((uint16_t)s_env[s_phase] * span) + 127u;   /* 四舍五入 */
        d = (uint8_t)((uint8_t)MODE3_DUTY_MIN + (uint8_t)(x / 255u));
    }
    effects_set_duty(d);
}

static uint16_t phase_ticks(void)
{
    uint16_t t = (uint16_t)(g_speed / MODE3_SPEED_DIV);
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
    s_hold  = (uint8_t)MODE3_DARK_HOLD_PHASES;   /* 上电从"全灭"开始，也停一下 */
    s_cnt   = 0u;
    apply_duty();
    telemetry_line("M3", "breath start");
}

void mode_breath_step(void)
{
    if (++s_cnt >= phase_ticks()) {
        s_cnt = 0u;
        if (s_hold > 0u) {
            s_hold--;                            /* 停在极值点不动 */
        } else {
            s_phase = (uint8_t)((s_phase + 1u) % BREATH_STEPS);
            if (s_phase == 0u) {
                /* 到最低点：灭着停一会儿 */
                s_hold = (uint8_t)MODE3_DARK_HOLD_PHASES;
            } else if (s_phase == (BREATH_STEPS / 2u)) {
                /* 到最高点：亮着停一会儿（和最低点对称，节奏才不歪） */
                s_hold = (uint8_t)MODE3_BRIGHT_HOLD_PHASES;
            }
        }
    }
    /* 每 tick 都要重设 duty：调度器在调用 step() 之前刚把它设成了亮度峰值 */
    apply_duty();
}
