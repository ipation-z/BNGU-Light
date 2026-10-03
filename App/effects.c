/**
  ******************************************************************************
  * @file    effects.c
  * @brief   模式调度
  *
  *  职责：
  *    1. 按键事件 → 切模式（带"闪 N 次"提示） / 切电位器控制对象
  *    2. 电位器 → 亮度峰值 / 速度（解析成全局参数）
  *    3. 每个 100Hz 节拍调用当前模式的 step()
  *
  *  模式表里的模式必须实现 init/step；没实现的模式（Phase C/D 的 MODE3/4）
  *  表项为空，调度器会自动跳过，KEY2 的循环顺序表里也暂时不列。
  ******************************************************************************
  */
#include "main.h"
#include <stdio.h>
#include "app_config.h"
#include "app.h"
#include "led_matrix.h"
#include "adc.h"
#include "telemetry.h"
#include "keys.h"
#include "effects.h"
#include "modes.h"

volatile uint8_t g_bright     = DUTY_DEFAULT;
volatile uint8_t g_speed      = SPEED_DEFAULT;
volatile uint8_t g_pot_target = POT_TARGET_BOTH;

typedef struct {
    const char *name;
    void (*init)(void);
    void (*step)(void);
} mode_desc_t;

static const mode_desc_t s_modes[MODE_COUNT] = {
    [MODE_WRITE]  = {"WRITE",  mode_write_init,  mode_write_step },
    [MODE_FLOW]   = {"FLOW",   mode_flow_init,   mode_flow_step  },
    [MODE_BREATH] = {"BREATH", mode_breath_init, mode_breath_step},
    [MODE_MIX]    = {"MIX",    mode_mix_init,    mode_mix_step   },
    [MODE_WALK]   = {"WALK",   mode_walk_init,   mode_walk_step  }
};

/* KEY2 的循环顺序（只列已经实现的模式，以后加模式只改这一行） */
static const uint8_t s_mode_order[] = {
    MODE_WRITE, MODE_FLOW, MODE_BREATH, MODE_MIX, MODE_WALK
};
#define MODE_ORDER_N   (sizeof(s_mode_order) / sizeof(s_mode_order[0]))

/* 闪灯提示状态 */
static uint8_t  s_pend_mode;
static uint8_t  s_flash_left;    /* 还要闪几次 */
static uint8_t  s_flash_phase;   /* 0 = 亮，1 = 灭 */
static uint8_t  s_flash_cnt;
static uint8_t  s_boot_anim;     /* 1 = 正在跑开机动画，跑完自动进 MODE1 */
static uint8_t  s_standby;       /* 1 = 开机动画跑完后的黑屏待机，等按键 */
static uint8_t  s_pot_linked;    /* 0 = 参数冻结（等电位器真的转动），1 = 跟手 */
static uint8_t  s_pot_ref;       /* 冻结时记录的电位器百分比 */

static void pot_freeze(void);    /* 前置声明：do_switch() 在它定义之前就要用 */

/*---------------------------------------------------------------------------*/
static const mode_desc_t *mode_desc(uint8_t m)
{
    if ((m >= MODE_COUNT) || (s_modes[m].step == NULL)) {
        return NULL;
    }
    return &s_modes[m];
}

/* 整屏点亮 / 熄灭（闪灯提示用） */
static void set_screen(uint8_t on)
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

void effects_set_duty(uint8_t d)
{
    uint8_t i;
    __disable_irq();
    for (i = 0u; i < LED_CHAR_COUNT; i++) {
        g_duty[i] = d;
    }
    __enable_irq();
}

/* 真正切换模式 */
static void do_switch(uint8_t m)
{
    const mode_desc_t *d = mode_desc(m);

    if (d == NULL) {
        return;
    }
    mode_walk_set_boot(0u);       /* 手动切到 MODE5 时是循环播放、跟电位器 */
    s_boot_anim = 0u;             /* 任何一次模式切换都取消"开机动画"身份 */
    g_mode = m;
    pot_freeze();                 /* 新模式从当前参数开始，等电位器动才跟手 */
    d->init();
    effects_set_duty(g_bright);
    telemetry_line("MODE", d->name);
}

void effects_set_mode_now(uint8_t m)
{
    if (m == g_mode) {
        return;
    }
    do_switch(m);
}

void effects_request_mode(uint8_t m)
{
    if (mode_desc(m) == NULL) {
        return;
    }
    /* 待机（黑屏等按键）时收到串口指令：直接叫醒并切过去。
       注意这里不能走下面的"已经是当前模式就返回" ——
       待机时 g_mode 还停在开机动画那档，会被误判成"已经切好了"，
       结果屏幕亮着却永远不推进（flash_tick 在待机分支里根本跑不到）。 */
    if (s_standby != 0u) {
        s_standby = 0u;
        telemetry_line("BOOT", "woken by serial MODE command");
    } else if (m == g_mode) {
        return;
    }
    s_pend_mode   = m;
    s_flash_left  = m;            /* 闪"模式号"次：MODE1 闪 1 下，MODE5 闪 5 下 */
    s_flash_phase = 0u;
    s_flash_cnt   = 0u;
    effects_set_duty(g_bright);   /* 闪灯期间也要有亮度 */
    set_screen(1u);
}

static uint8_t next_mode(void)
{
    uint8_t i;
    for (i = 0u; i < MODE_ORDER_N; i++) {
        if (s_mode_order[i] == g_mode) {
            return s_mode_order[(i + 1u) % MODE_ORDER_N];
        }
    }
    return s_mode_order[0];
}

/* 返回 1 表示还在闪灯提示中 */
static uint8_t flash_tick(void)
{
    uint16_t limit;

    if (s_flash_left == 0u) {
        return 0u;
    }

    limit = (s_flash_phase == 0u) ? (uint16_t)FLASH_ON_TICKS : (uint16_t)FLASH_OFF_TICKS;
    if (++s_flash_cnt < limit) {
        return 1u;
    }
    s_flash_cnt = 0u;

    if (s_flash_phase == 0u) {
        s_flash_phase = 1u;
        set_screen(0u);
        s_flash_left--;
        if (s_flash_left == 0u) {
            do_switch(s_pend_mode);   /* 闪完 → 切模式 */
            return 0u;
        }
    } else {
        s_flash_phase = 0u;
        set_screen(1u);
    }
    return 1u;
}

/*---------------------------------------------------------------------------*/
/* 电位器解析                                                                 */
/*---------------------------------------------------------------------------*/
static uint8_t pot_pct(uint16_t mv)
{
    if (mv <= POT_MV_MIN) {
        return 0u;
    }
    if (mv >= POT_MV_MAX) {
        return 100u;
    }
    return (uint8_t)(((uint32_t)(mv - POT_MV_MIN) * 100u) / (POT_MV_MAX - POT_MV_MIN));
}

/* 平方律：让人眼感觉到的亮度大致跟着电位器线性变化。
   最低给 BRIGHT_MIN 级（不是 1 级也不是全黑）—— 1 级时平均电流只有 0.15mA，
   亮房间里看着就是黑的，容易被误判成故障。 */
static uint8_t pct_to_bright(uint8_t p)
{
    uint32_t x = (uint32_t)p * (uint32_t)p;          /* 0..10000 */
    return (uint8_t)(BRIGHT_MIN
                     + ((x * (uint32_t)(DUTY_MAX - BRIGHT_MIN)) / 10000u));
}

/* 速度映射表：索引 = 电位器百分比 / 5（0..20），值 = tick 数/步（1 tick = 10ms）。
   按等比分布，所以每转过一格，速度变化的"比例"差不多，手感均匀；
   中间位置 ≈ SPEED_DEFAULT(12) = 120ms/步。 */
static const uint8_t s_speed_tab[21] = {
    50u, 43u, 36u, 31u, 26u, 22u, 19u, 16u, 14u, 12u, 10u,
     9u,  7u,  6u,  5u,  5u,  4u,  3u,  3u,  2u,  2u
};

/* 顺时针（电压高）= 更快 = tick 数更小 */
static uint8_t pct_to_speed(uint8_t p)
{
    return s_speed_tab[p / 5u];
}

/* 软接管：把参数冻结在当前值，等电位器真的转过 POT_TAKEOVER_PCT 才重新跟手。
   切 KEY3 档位、切模式、刚上电、退出待机时都调用它，保证参数不会突然跳变。 */
static void pot_freeze(void)
{
    s_pot_ref    = pot_pct(adc_mv(ADC_CH_POT));
    s_pot_linked = 0u;
}

static void pot_update(void)
{
    uint8_t p = pot_pct(adc_mv(ADC_CH_POT));

    if (s_pot_linked == 0u) {
        uint8_t d = (p > s_pot_ref) ? (uint8_t)(p - s_pot_ref)
                                    : (uint8_t)(s_pot_ref - p);
        if (d < POT_TAKEOVER_PCT) {
            return;                        /* 电位器还没动，保持冻结 */
        }
        s_pot_linked = 1u;
        telemetry_line("POT", "takeover - now following the pot");
    }

    switch (g_pot_target) {
    case POT_TARGET_BRIGHT:
        /* 只调亮度：速度保持不动（不跳回默认值） */
        g_bright = pct_to_bright(p);
        break;
    case POT_TARGET_SPEED:
        /* 只调速度：亮度保持不动（不跳回默认值） */
        g_speed = pct_to_speed(p);
        break;
    default:                               /* POT_TARGET_BOTH：联动 */
        g_bright = pct_to_bright(p);
        g_speed  = pct_to_speed(p);
        break;
    }
}

/*---------------------------------------------------------------------------*/
/* 串口 / 蓝牙控制（App/cli.c 调用）                                          */
/*---------------------------------------------------------------------------*/

/* 待机（黑屏等按键）时被串口指令叫醒：进入默认模式 MODE1。
   不做这一步的话，待机期间发 BRIGHT/SPEED 只会改参数而屏幕依旧全黑，
   用户会以为"蓝牙没反应"。 */
void effects_wake(void)
{
    if (s_standby != 0u) {
        s_standby = 0u;
        telemetry_line("BOOT", "woken by serial command -> MODE1 WRITE");
        do_switch(MODE_WRITE);
    }
}

/* 按百分比设置亮度峰值。走的是和电位器完全相同的平方律，
   所以"蓝牙发 50%"和"电位器拧到中间"效果一模一样。
   设完立刻调 pot_freeze() 冻结软接管 —— 这是关键：
   pot_update() 每 10ms 跑一次，不冻结的话刚才设的值马上就被 ADC 覆盖掉。 */
void effects_set_bright_pct(uint8_t pct)
{
    if (pct > 100u) {
        pct = 100u;
    }
    effects_wake();
    g_bright = pct_to_bright(pct);
    pot_freeze();
}

void effects_set_speed_pct(uint8_t pct)
{
    if (pct > 100u) {
        pct = 100u;
    }
    effects_wake();
    g_speed = pct_to_speed(pct);
    pot_freeze();
}

/* 反算当前值对应的百分比（ASK 回读用）。
   档位只有 16 级，所以"能产生当前档位的百分比"是一段区间，
   取区间中点 —— 这样发 BRIGHT50 就能读回 50，而不是 49。
   只在收到 ASK 指令时跑一次，200 次循环的开销可以忽略。 */
uint8_t effects_bright_pct(void)
{
    uint8_t lo = 100u;
    uint8_t hi = 100u;
    uint8_t p;

    for (p = 0u; p <= 100u; p++) {
        if (pct_to_bright(p) >= g_bright) {
            lo = p;
            break;
        }
    }
    for (p = lo; p <= 100u; p++) {
        if (pct_to_bright(p) != g_bright) {
            break;
        }
        hi = p;
    }
    return (uint8_t)(((uint16_t)lo + (uint16_t)hi) / 2u);
}

uint8_t effects_speed_pct(void)
{
    uint8_t lo = 100u;
    uint8_t hi = 100u;
    uint8_t p;

    /* 百分比越大 = tick 越小 = 越快，所以这里是"小于等于" */
    for (p = 0u; p <= 100u; p++) {
        if (pct_to_speed(p) <= g_speed) {
            lo = p;
            break;
        }
    }
    for (p = lo; p <= 100u; p++) {
        if (pct_to_speed(p) != g_speed) {
            break;
        }
        hi = p;
    }
    return (uint8_t)(((uint16_t)lo + (uint16_t)hi) / 2u);
}

/* 1 = 电位器正在跟手（= 用户手动调节中）
   0 = 参数已冻结（蓝牙刚设的值保持中，或刚切档/切模式） */
uint8_t effects_pot_is_linked(void)
{
    return s_pot_linked;
}

/*---------------------------------------------------------------------------*/
const char *effects_mode_name(void)
{
    const mode_desc_t *d = mode_desc(g_mode);
    return (d != NULL) ? d->name : "?";
}

const char *effects_pot_name(void)
{
    switch (g_pot_target) {
    case POT_TARGET_BRIGHT: return "BRIGHT";
    case POT_TARGET_SPEED:  return "SPEED";
    default:                return "BOTH";
    }
}

/*---------------------------------------------------------------------------*/
void effects_init(void)
{
    /* 初值 = 开机动画用的参数（满档 19、120ms/点）。
       开机动画和待机期间 pot_update() 根本不跑，所以这两个值会原样冻住；
       退出待机切模式时 do_switch() 里的 pot_freeze() 会以此为起点，
       等电位器真的转动了才重新跟手 —— 所以从待机到 MODE1 不会有跳变。 */
    g_bright      = (uint8_t)DUTY_MAX;
    g_speed       = (uint8_t)POS_PER_DOT;
    g_pot_target  = POT_TARGET_BOTH;
    s_flash_left  = 0u;
    s_flash_phase = 0u;
    s_flash_cnt   = 0u;
    s_pend_mode   = MODE_WRITE;
    s_standby     = 0u;
    s_pot_linked  = 0u;
    s_pot_ref     = 0u;

    /* 开机动画 = MODE5 描边游走，走速和亮度都固定（不跟电位器），
       保证每次上电的自检表现完全一致；
       跑完一轮后 effects_tick() 会进入黑屏待机，等按键才开始。 */
    s_boot_anim = 1u;
    g_mode      = MODE_WALK;
    mode_walk_set_boot(1u);
    mode_walk_init();
    effects_set_duty(g_bright);
}

void effects_tick(void)
{
    uint8_t ev = keys_take_event();
    const mode_desc_t *d;

    /* ① 黑屏待机：任意键才开始（避免动画一结束就自己跑起来） */
    if (s_standby != 0u) {
        if (ev != KEY_EV_NONE) {
            s_standby = 0u;
            telemetry_line("BOOT", "key pressed -> MODE1 WRITE");
            do_switch(MODE_WRITE);
        }
        return;                    /* 待机期间不跟电位器、不跑模式 */
    }

    /* ② 按键 */
    if ((ev & KEY_EV_KEY2) != 0u) {
        effects_request_mode(next_mode());
    }
    if ((ev & KEY_EV_KEY3) != 0u) {
        char msg[56];
        g_pot_target = (uint8_t)((g_pot_target + 1u) % POT_TARGET_COUNT);
        pot_freeze();              /* 换档后先冻结，等电位器动才跟手 */
        (void)snprintf(msg, sizeof msg, "POT=%s FROZEN  BRIGHT=%u/%u SPEED=%u",
                       effects_pot_name(),
                       (unsigned)g_bright, (unsigned)DUTY_MAX, (unsigned)g_speed);
        telemetry_line("KEY3", msg);
    }

    /* ③ 闪灯提示期间不跑模式 */
    if (flash_tick() != 0u) {
        return;
    }

    /* ④ 电位器 → 亮度峰值 / 速度（开机动画期间不跟电位器） */
    if (s_boot_anim == 0u) {
        pot_update();
    }

    /* ⑤ 默认整屏亮度 = 亮度峰值；模式内部可以再覆盖（例如 MODE5 的渐亮/渐灭） */
    effects_set_duty(g_bright);

    /* ⑥ 跑当前模式 */
    d = mode_desc(g_mode);
    if (d != NULL) {
        d->step();
    }

    /* ⑦ 开机动画跑完一轮 → 全黑待机，等按键才开始。
       放在调度器里而不是模式内部，这样"跑完"这件事一定被处理到。 */
    if ((s_boot_anim != 0u) && (mode_walk_take_done() != 0u)) {
        s_boot_anim = 0u;
        s_standby   = 1u;
        set_screen(0u);            /* 确保全黑 */
        telemetry_line("BOOT", "animation done - standby, press any key");
    }
}
