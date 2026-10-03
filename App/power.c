/**
  ******************************************************************************
  * @file    power.c
  * @brief   一键开关机：按一下上电，再按一下关机
  *
  *  ---------- 硬件是怎么工作的 ----------
  *  电源通路上有一个 PMOS 自锁：按键把 PMOS 打开 → 3.3V 起来 → MCU 初始化时
  *  把 KEY1(PB11) 拉高 → 经 NMOS 把 PMOS 栅极持续拉低 → 松手后装置继续供电。
  *  所以"关机" = 把 KEY1 拉低，松开自锁，PMOS 关断，整机掉电。
  *
  *  ADC1(PA0) 一直在量按键两端的电压：
  *      未按下 ≈ 0V（按键把这点接地）
  *      按下   ≈ 2.0~2.6V
  *
  *  ---------- 关键：按下上电不能立刻又关机 ----------
  *  按一下上电时，按键是**一直按着**的，MCU 上电跑起来的那一刻 ADC1 就是高电平。
  *  如果直接检测"高电平 → 关机"，就会一开机立刻自己关掉。
  *
  *  所以加了 PWR_WAIT_RELEASE 状态：上电后必须先连续读到"松开"（ADC 回到 0V），
  *  才进入 PWR_ARMED 开始允许关机。在那之前按多久都不会关。
  *
  *  另外还有超时保护（PWR_RELEASE_TIMEOUT）：如果 5 秒了还一直读到高电平
  *  （按键卡住 / ADC1 还没接线 / PA0 悬空），就进入 PWR_DISABLED 彻底放弃
  *  关机检测，并打一行日志。宁可按键关不掉（拔电就行），也绝不能一开机就关机。
  *
  *  ---------- 状态机 ----------
  *     上电
  *      ↓
  *   WAIT_RELEASE ──读到低电平 30ms──→ ARMED ──读到高电平 300ms──→ 关机时序
  *      │                                  ↑          │
  *      └──5s 一直高──→ DISABLED           └──读到低──┘（计数清零）
  ******************************************************************************
  */
#include "main.h"
#include "app_config.h"
#include "adc.h"
#include "led_matrix.h"
#include "telemetry.h"
#include "effects.h"
#include "power.h"

typedef enum {
    PWR_WAIT_RELEASE = 0,   /* 上电后先等按键松开 */
    PWR_ARMED,              /* 已武装：再按一下（300ms）就关机 */
    PWR_DISABLED,           /* 松开检测超时：放弃关机检测 */
    PWR_OFF                 /* 已经执行关机 */
} pwr_state_t;

static pwr_state_t s_state;
static uint16_t    s_cnt;    /* 松开确认计数 / 关机长按计数 */
static uint16_t    s_to;     /* 松开超时计数 */
static uint16_t    s_start;  /* 上电静默期剩余节拍 */

/*---------------------------------------------------------------------------*/
/* 关机时序：永不返回 */
static void do_shutdown(void)
{
    s_state = PWR_OFF;          /* 先置位，保证中断重入也不会再走一遍 */

    telemetry_line("PWR", "shutdown - releasing latch");
    HAL_Delay(10);              /* 让这行字从串口发出去（10ms，用户感觉不到） */

    /* ① 停掉三个定时器：之后不会再有扫描中断来改 GPIO */
    (void)HAL_TIM_Base_Stop_IT(&htim1);
    (void)HAL_TIM_Base_Stop_IT(&htim2);
    (void)HAL_TIM_Base_Stop_IT(&htim3);

    /* ② 灯全灭 + 12 个栅极进安全态（阴极全低、4 个 PMOS 全关） */
    effects_set_duty(0u);
    led_matrix_off();

    /* ③ 松开自锁：KEY1 拉低 → NMOS 关断 → PMOS 栅极被上拉 → 整机掉电 */
    HAL_GPIO_WritePin(KEY1_GPIO_Port, KEY1_Pin, GPIO_PIN_RESET);

    /* ④ 等 3.3V 轨塌下去。SysTick 也停掉，让 CPU 彻底歇着不再耗电 */
    HAL_SuspendTick();
    for (;;) {
        __WFI();
    }
}

/*---------------------------------------------------------------------------*/
void power_init(void)
{
    s_state = PWR_WAIT_RELEASE;
    s_cnt   = 0u;
    s_to    = 0u;
    s_start = (uint16_t)PWR_STARTUP_IGNORE_TICKS;

    telemetry_line("PWR", "waiting for key release (press-to-on latch)");
}

uint8_t power_is_off(void)
{
    return (s_state == PWR_OFF) ? 1u : 0u;
}

const char *power_state_name(void)
{
    switch (s_state) {
    case PWR_WAIT_RELEASE: return "WAIT";
    case PWR_ARMED:        return "ARMED";
    case PWR_DISABLED:     return "DISAB";
    default:               return "OFF";
    }
}

void power_tick(void)
{
    uint16_t mv;

    if (s_state == PWR_OFF) {
        return;
    }

    /* 上电静默期：这会儿 ADC 缓冲还是初值 0、3.3V 轨也未必稳，
       读到什么都别当真，否则会被"假低电平"骗进 ARMED 然后自己关机。 */
    if (s_start > 0u) {
        s_start--;
        return;
    }

    mv = adc_mv(ADC_CH_KEY);

    switch (s_state) {

    /* 上电后按键还按着，先等它松开 —— 这一步保证不会"一开机就关机" */
    case PWR_WAIT_RELEASE:
        if (mv < PWR_KEY_MV_THRESHOLD) {
            s_to = 0u;
            if (++s_cnt >= PWR_RELEASE_CONFIRM) {
                s_cnt   = 0u;
                s_state = PWR_ARMED;
                telemetry_line("PWR", "key released - power-off armed");
            }
        } else {
            s_cnt = 0u;
            if (++s_to >= PWR_RELEASE_TIMEOUT) {
                s_state = PWR_DISABLED;
                s_to    = 0u;
                telemetry_line("PWR", "key still HIGH - power-off DISABLED");
            }
        }
        break;

    /* 已武装：按住 300ms 就关机 */
    case PWR_ARMED:
        if (mv >= PWR_KEY_MV_THRESHOLD) {
            if (++s_cnt >= PWR_OFF_HOLD_TICKS) {
                do_shutdown();
            }
        } else {
            s_cnt = 0u;
        }
        break;

    default:                    /* PWR_DISABLED：什么都不做 */
        break;
    }
}
