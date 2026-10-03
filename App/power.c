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
#include <stdio.h>
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
/* 关机时序 */
static void do_shutdown(void)
{
    uint8_t  i;
    uint8_t  k1;
    uint16_t mv;
    char     msg[80];

    s_state = PWR_OFF;          /* 先置位，保证中断重入也不会再走一遍 */

    /* 等发送空闲（最多 30ms），保证关机提示一定发得出去而不是被丢弃 */
    for (i = 0u; i < 15u; i++) {
        if (telemetry_tx_idle() != 0u) {
            break;
        }
        HAL_Delay(2);
    }

    /* ① 停掉三个定时器：之后不会再有扫描中断来改 GPIO */
    (void)HAL_TIM_Base_Stop_IT(&htim1);
    (void)HAL_TIM_Base_Stop_IT(&htim2);
    (void)HAL_TIM_Base_Stop_IT(&htim3);

    /* ② 灯全灭 + 12 个栅极进安全态（阴极全低、4 个 PMOS 全关） */
    effects_set_duty(0u);
    led_matrix_off();

    /* ③ 关断 BSS138：KEY1(PB11) 拉低。
          BSS138 是 N 沟道：栅极高 = 导通 = 开机，栅极低 = 不导通 = 关机。 */
    HAL_GPIO_WritePin(KEY1_GPIO_Port, KEY1_Pin, GPIO_PIN_RESET);

    /* ④ 回读一次，把"软件到底做没做"这件事钉死。
          用 HAL_GPIO_ReadPin 直接读 IDR，能确认引脚真的变成低电平 ——
          而不是只相信我们写过 BSRR。 */
    k1 = (HAL_GPIO_ReadPin(KEY1_GPIO_Port, KEY1_Pin) == GPIO_PIN_SET) ? 1u : 0u;
    mv = adc_mv(ADC_CH_KEY);
    (void)snprintf(msg, sizeof msg,
        "KEY1/PB11=%s  key=%umV %s  -> BSS138 should be OFF",
        (k1 != 0u) ? "HIGH(!)" : "LOW",
        (unsigned)mv,
        (mv >= PWR_KEY_MV_THRESHOLD) ? "(button STILL HELD)" : "(button released)");
    telemetry_line("PWR", msg);

    /* ⑤ 等电源轨塌陷。
          正常情况下 BSS138 一关断，3.3V 在几十 ms 内就没了，MCU 直接断电，
          根本跑不到下面这段。
          如果**没塌**，说明还有别的通路把 BSS138 的栅极拉着 —— 最常见的就是
          电源键还按着（按键与 KEY1 并联在栅极上）。这时我们仍然活着，
          就把实情打出来，而不是一声不响地卡在死循环里让人无从下手。 */
    for (i = 0u; i < 30u; i++) {        /* 30 × 10ms = 300ms */
        HAL_Delay(10);
    }
    telemetry_line("PWR", "STILL POWERED 300ms after KEY1 went LOW");
    telemetry_line("PWR", "check 1: button still held?  check 2: BSS138 gate pulled up too hard?");

    /* ⑥ 继续等。KEY1 已经是低电平，只要栅极上那条通路消失（松手/硬件正常），
          电源立刻就会断。把 SysTick 停掉，让 CPU 彻底歇着不再耗电。 */
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
                telemetry_line("PWR", "key held >5s - waiting for release to re-arm");
            }
        }
        break;

    /* 已武装：按住 PWR_OFF_HOLD_TICKS 就关机（不等松手） */
    case PWR_ARMED:
        if (mv >= PWR_KEY_MV_THRESHOLD) {
            if (s_cnt == 0u) {
                /* 按下沿打一行带实际 mV 的日志：
                   如果按了却没看到这行，说明 ADC 根本没读到按键，问题在采样侧 */
                char msg[48];
                (void)snprintf(msg, sizeof msg, "key %umV pressed - hold %uticks to off",
                               (unsigned)mv, (unsigned)PWR_OFF_HOLD_TICKS);
                telemetry_line("PWR", msg);
            }
            if (++s_cnt >= PWR_OFF_HOLD_TICKS) {
                do_shutdown();
            }
        } else {
            if (s_cnt > 0u) {
                telemetry_line("PWR", "released before threshold - cancelled");
            }
            s_cnt = 0u;
        }
        break;

    /* 超时后曾经放弃过关机检测。但按键随时可能被松开 ——
       一旦读到松开就立刻重新武装。
       这一条很关键：上电时按键本来就是按住的，如果按住的时间超过了超时时间
       （比如按着电源键看那 5.8 秒的开机自检动画），旧版本会永久停在
       DISABLED，导致这次上电**再也关不掉**，只能拔电。 */
    default:                    /* PWR_DISABLED */
        if (mv < PWR_KEY_MV_THRESHOLD) {
            s_state = PWR_ARMED;
            s_cnt   = 0u;
            telemetry_line("PWR", "key released - power-off re-armed");
        }
        break;
    }
}
