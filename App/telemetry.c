/**
  ******************************************************************************
  * @file    telemetry.c
  ******************************************************************************
  */
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "app.h"
#include "adc.h"
#include "led_matrix.h"
#include "telemetry.h"

/* DMA 发送期间这个缓冲必须一直有效，所以只能有一份、且只能静态 */
static char              s_txbuf[TLM_BUF_SIZE];
static volatile uint8_t  s_tx_busy;
static uint16_t          s_busy_ticks;   /* 看门狗：防止忙标志卡死 */
static uint8_t           s_fail_cnt;

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
    s_tx_busy = 0u;
    s_busy_ticks = 0u;
    s_fail_cnt = 0u;
    s_txbuf[0] = '\0';
    telemetry_line("BOOT", "BNGU_Light v1.0  Phase-A: scan engine + selftest");
}

void telemetry_line(const char *tag, const char *msg)
{
    if (s_tx_busy != 0u) {
        return;                     /* 上一帧没发完，丢掉本行，绝不阻塞 */
    }
    (void)snprintf(s_txbuf, sizeof s_txbuf, "[%-4s] %s\r\n", tag, msg);
    tlm_send();
}

void telemetry_tick(void)
{
    if (s_tx_busy != 0u) {
        if (++s_busy_ticks > 100u) {     /* 超过 1s 还没完成 → 强制解锁 */
            s_busy_ticks = 0u;
            s_tx_busy = 0u;
        }
        return;
    }
    /* 亮度取 4 个字符中的最大值（槽起点统一预计算，四个值一致） */
    (void)snprintf(s_txbuf, sizeof s_txbuf,
        "[TLM ] MODE=%u BRIGHT=%u/%u SPEED=%u ADC1=%umV ADC2=%umV\r\n",
        (unsigned)g_mode,
        (unsigned)g_duty[0],
        (unsigned)DUTY_MAX,
        (unsigned)g_speed,
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

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        s_tx_busy = 0u;
        s_busy_ticks = 0u;
    }
}
