/**
  ******************************************************************************
  * @file    telemetry.c
  * @brief   串口输出：事件行 / 10Hz 周期帧 / 串口指令回包队列
  *
  *  只有一个静态发送缓冲（DMA 发送期间它必须一直有效），
  *  再加一个 4 行的待发队列 —— 一次要回好几行（比如 HELP）时排队，
  *  由 telemetry_pump() 每 10ms 发一行，不会被忙标志吃掉。
  ******************************************************************************
  */
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "app.h"
#include "adc.h"
#include "led_matrix.h"
#include "effects.h"
#include "power.h"
#include "telemetry.h"

/* DMA 发送期间这个缓冲必须一直有效，所以只能有一份、且只能静态 */
static char              s_txbuf[TLM_BUF_SIZE];
static volatile uint8_t  s_tx_busy;
static uint16_t          s_busy_ticks;   /* 看门狗：防止忙标志卡死 */
static uint8_t           s_fail_cnt;

/* 待发队列（每行一份缓冲） */
static char              s_q[TLM_QUEUE_N][TLM_BUF_SIZE];
static uint8_t           s_q_head;
static uint8_t           s_q_tail;
static uint8_t           s_q_cnt;

/* 10Hz 周期帧开关：默认关 */
static uint8_t           s_periodic_on;

static void tlm_send(void)
{
    uint16_t len = (uint16_t)strlen(s_txbuf);

    if (len == 0u || s_tx_busy != 0u) {
        return;
    }
    s_tx_busy = 1u;
    if (HAL_UART_Transmit_DMA(&huart1, (uint8_t *)s_txbuf, len) != HAL_OK) {
        /* HAL 内部状态卡住时主动复位一次，避免遥测永久静默 */
        if (++s_fail_cnt >= 5u) {
            s_fail_cnt = 0u;
            (void)HAL_UART_AbortTransmit(&huart1);
        }
        s_tx_busy = 0u;
        return;
    }
    s_fail_cnt = 0u;
}

void telemetry_init(void)
{
    s_tx_busy     = 0u;
    s_busy_ticks  = 0u;
    s_fail_cnt    = 0u;
    s_q_head      = 0u;
    s_q_tail      = 0u;
    s_q_cnt       = 0u;
    s_periodic_on = 0u;          /* 10Hz 周期帧默认关 */
    s_txbuf[0]    = '\0';

    /* ---- 开机提示 + 指令提示 ----
       必须走队列：telemetry_line() 是"发送忙就丢弃"，
       连续 4 行的话后 3 行会被前一行占着的 DMA 通道吃掉。
       队列由 telemetry_pump() 每 10ms 发一行。 */
    telemetry_queue("BOOT", "BNGU_Light v1.0  Phase-F: BT serial control");
    telemetry_queue("BOOT", "UART1 115200 8N1  telemetry OFF by default (TLM1 to enable)");
    telemetry_queue("BT", "MODE<n> n=1..5 | BRIGHT<n> n=0..100 | SPEED<n> n=0..100");
    telemetry_queue("BT", "ASK MODE | ASK BRIGHT | ASK SPEED | TLM<0|1> | HELP");
}

void telemetry_line(const char *tag, const char *msg)
{
    /* 上一帧还在发、或队列里还有没发完的行 → 排到队列后面去。
       这样既不会丢消息，也不会出现"后发生的行插到开机横幅前面"这种乱序。 */
    if (s_tx_busy != 0u || s_q_cnt != 0u) {
        telemetry_queue(tag, msg);
        return;
    }
    (void)snprintf(s_txbuf, sizeof s_txbuf, "[%-4s] %s\r\n", tag, msg);
    tlm_send();
}

void telemetry_queue(const char *tag, const char *msg)
{
    if (s_q_cnt >= TLM_QUEUE_N) {
        return;                     /* 队列满，丢掉这一行 */
    }
    (void)snprintf(s_q[s_q_tail], TLM_BUF_SIZE, "[%-4s] %s\r\n", tag, msg);
    s_q_tail = (uint8_t)((s_q_tail + 1u) % TLM_QUEUE_N);
    s_q_cnt++;
}

void telemetry_pump(void)
{
    if (s_q_cnt == 0u || s_tx_busy != 0u) {
        return;                     /* 没有待发行，或上一行还在发 */
    }
    (void)memcpy(s_txbuf, s_q[s_q_head], TLM_BUF_SIZE);
    s_txbuf[TLM_BUF_SIZE - 1u] = '\0';
    s_q_head = (uint8_t)((s_q_head + 1u) % TLM_QUEUE_N);
    s_q_cnt--;
    tlm_send();
}

void telemetry_tx_reset(void)
{
    s_tx_busy    = 0u;
    s_busy_ticks = 0u;
}

uint8_t telemetry_tx_idle(void)
{
    /* 发送空闲 **且** 队列已空 —— 只有这时 telemetry_line() 才会立刻发出去 */
    return (s_tx_busy == 0u && s_q_cnt == 0u) ? 1u : 0u;
}

void telemetry_set_periodic(uint8_t on)
{
    s_periodic_on = (on != 0u) ? 1u : 0u;
}

uint8_t telemetry_periodic_is_on(void)
{
    return s_periodic_on;
}

void telemetry_tick(void)
{
    if (s_periodic_on == 0u) {
        return;                     /* 默认关闭：串口上只留事件行和指令回包 */
    }
    if (s_tx_busy != 0u) {
        if (++s_busy_ticks > 100u) {     /* 超过 1s 还没完成 → 强制解锁 */
            s_busy_ticks = 0u;
            s_tx_busy = 0u;
        }
        return;
    }
    /* BRIGHT 报的是"亮度峰值参数"，不是当前实际 duty
       （MODE5 渐亮/渐灭期间实际 duty 会临时低于它） */
    (void)snprintf(s_txbuf, sizeof s_txbuf,
        "[TLM ] MODE=%u(%s) BRIGHT=%u/%u SPEED=%u POT=%s PWR=%s ADC1=%umV ADC2=%umV\r\n",
        (unsigned)g_mode,
        effects_mode_name(),
        (unsigned)g_bright,
        (unsigned)DUTY_MAX,
        (unsigned)g_speed,
        effects_pot_name(),
        power_state_name(),
        (unsigned)adc_mv(ADC_CH_KEY),
        (unsigned)adc_mv(ADC_CH_POT));
    tlm_send();
}

/*---------------------------------------------------------------------------*/
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        s_tx_busy = 0u;
        s_busy_ticks = 0u;
    }
}

/* 注意：HAL_UART_ErrorCallback 放在 App/cli.c ——
   那里既要清发送忙标志（调 telemetry_tx_reset），又要重新武装接收。 */
