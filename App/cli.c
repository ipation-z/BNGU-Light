/**
  ******************************************************************************
  * @file    cli.c
  * @brief   串口 / 蓝牙指令解析（详见 cli.h）
  *
  *  ---------- 为什么用"接收直到空闲"而不是逐字节中断 ----------
  *  HAL_UARTEx_ReceiveToIdle_IT() 有两个好处：
  *    1. HAL 内部自动续接收，不像逐字节 Receive_IT 那样要应用层反复重新武装，
  *       主循环慢一点也不会丢字节；
  *    2. 自带 IDLE 空闲线检测 —— 线上安静一个字节时间就回调一次，
  *       于是**手机端不加换行也能识别出一条指令**。
  *
  *  115200 下字节率 11520/s，每字节一次中断约 4µs，连续满速收也只占 4.6% CPU，
  *  所以没必要上 RX DMA。RX DMA 要回 CubeMX 重新分配 DMA1_Channel5 才行。
  *
  *  ---------- 中断和主循环的分工 ----------
  *  中断里只做"把字节拼成一行、置标志"，解析和执行全在 cli_tick()（主循环 100Hz）。
  *  和整个工程"中断只置标志、主循环做事"的架构一致 ——
  *  这样在中断里下断点不会打乱 LED 扫描的 PWM 时序。
  ******************************************************************************
  */
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "app.h"
#include "effects.h"
#include "telemetry.h"
#include "cli.h"

/* HAL 直接往里写 */
static uint8_t  s_rx[CLI_RX_SIZE];

static char     s_build[CLI_LINE_MAX];   /* 正在拼的行 */
static char     s_rdy[CLI_LINE_MAX];     /* 拼好的行，等主循环处理 */
static uint8_t  s_len;                   /* s_build 里已有多少字符 */
static uint8_t  s_ovf;                   /* 1 = 这一行已经超长 */

static volatile uint8_t  s_ready;        /* 1 = s_rdy 里有一整行待处理 */
static volatile uint8_t  s_bad;          /* 1 = 刚提交的那行是"超长"错误 */
static volatile uint8_t  s_rearm;        /* 1 = 需要重新武装接收 */
static volatile uint32_t s_last_tick;    /* 最后一个字节到达的节拍 */

/*---------------------------------------------------------------------------*/
/* 把 s_build 提交成一条待处理指令 */
static void cli_commit(void)
{
    if (s_ready != 0u) {
        s_len = 0u;             /* 上一行还没处理完，这行丢掉 */
        s_ovf = 0u;
        return;
    }
    if (s_ovf != 0u) {
        s_len = 0u;
        s_ovf = 0u;
        s_bad = 1u;
        s_ready = 1u;
        return;
    }
    if (s_len == 0u) {
        return;
    }
    (void)memcpy(s_rdy, s_build, s_len);
    s_rdy[s_len] = '\0';
    s_len = 0u;
    s_ready = 1u;
}

static void cli_push(char ch)
{
    if (ch == '\r' || ch == '\n') {
        cli_commit();
        return;
    }
    if (s_ovf != 0u) {
        return;                 /* 已经超长，剩下的字符直接扔 */
    }
    if (s_len >= (CLI_LINE_MAX - 1u)) {
        s_ovf = 1u;
        return;
    }
    s_build[s_len++] = ch;
}

/*---------------------------------------------------------------------------*/
/* 小工具                                                                     */

static void upcase(char *s)
{
    while (*s != '\0') {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char)(*s - 32);
        }
        s++;
    }
}

/* 解析十进制整数。s 必须是"纯数字"（前后允许空格），否则返回 0 */
static uint8_t parse_u16(const char *s, uint16_t *out)
{
    uint32_t v = 0u;
    uint8_t  n = 0u;

    while (*s == ' ') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t)(*s - '0');
        if (v > 65535u) {
            return 0u;
        }
        s++;
        n++;
    }
    while (*s == ' ') {
        s++;
    }
    if (n == 0u || *s != '\0') {
        return 0u;              /* 没有数字，或数字后面还有杂物（比如 MODE3X） */
    }
    *out = (uint16_t)v;
    return 1u;
}

/* 控制权归属：电位器跟手中 = 手动调节；冻结 = 蓝牙/上次设定的值保持中 */
static const char *ctrl_str(void)
{
    return (effects_pot_is_linked() != 0u) ? "POT" : "HOLD";
}

static void reply(const char *msg)
{
    telemetry_queue("BT", msg);
}

/*---------------------------------------------------------------------------*/
/* ASK xxx                                                                    */
static void cli_ask(const char *what)
{
    char msg[100];

    if (strcmp(what, "MODE") == 0) {
        (void)snprintf(msg, sizeof msg, "MODE=%u(%s)  CTRL=%s  KEY3=%s",
                       (unsigned)g_mode, effects_mode_name(), ctrl_str(),
                       effects_pot_name());

    } else if (strcmp(what, "BRIGHT") == 0) {
        (void)snprintf(msg, sizeof msg, "BRIGHT=%u%% (%u/%u)  CTRL=%s  KEY3=%s",
                       (unsigned)effects_bright_pct(), (unsigned)g_bright,
                       (unsigned)DUTY_MAX, ctrl_str(), effects_pot_name());

    } else if (strcmp(what, "SPEED") == 0) {
        (void)snprintf(msg, sizeof msg, "SPEED=%u%% (%u)  CTRL=%s  KEY3=%s",
                       (unsigned)effects_speed_pct(), (unsigned)g_speed,
                       ctrl_str(), effects_pot_name());

    } else {
        (void)snprintf(msg, sizeof msg,
            "ERR ASK needs MODE | BRIGHT | SPEED  (got '%s')", what);
    }
    reply(msg);
}

/*---------------------------------------------------------------------------*/
static void cli_exec(char *line)
{
    uint16_t v;
    char     msg[100];

    upcase(line);

    /* ---- ASK MODE / ASK BRIGHT / ASK SPEED ---- */
    if (strncmp(line, "ASK", 3u) == 0) {
        char *sp = line + 3;
        while (*sp == ' ') {
            sp++;
        }
        cli_ask(sp);
        return;
    }

    /* ---- MODE<n> ---- */
    if (strncmp(line, "MODE", 4u) == 0) {
        if (parse_u16(line + 4, &v) == 0u) {
            reply("ERR MODE needs a number 1..5  (e.g. MODE3)");
            return;
        }
        if (v < 1u || v > 5u) {
            (void)snprintf(msg, sizeof msg, "ERR MODE=%u out of range 1..5", (unsigned)v);
            reply(msg);
            return;
        }
        if ((uint8_t)v == g_mode) {
            /* 已经是这个模式了。不要回"闪 n 下"，否则用户会奇怪为什么没闪 */
            (void)snprintf(msg, sizeof msg, "OK MODE=%u (already active, no flash)", (unsigned)v);
            reply(msg);
            return;
        }
        effects_request_mode((uint8_t)v);   /* 复用 KEY2 那条路：会闪 n 下做确认 */
        (void)snprintf(msg, sizeof msg, "OK MODE=%u (flashing %u times)", (unsigned)v, (unsigned)v);
        reply(msg);
        return;
    }

    /* ---- BRIGHT<n> ---- */
    if (strncmp(line, "BRIGHT", 6u) == 0) {
        if (parse_u16(line + 6, &v) == 0u) {
            reply("ERR BRIGHT needs a number 0..100  (e.g. BRIGHT50)");
            return;
        }
        if (v > 100u) {
            (void)snprintf(msg, sizeof msg, "ERR BRIGHT=%u out of range 0..100", (unsigned)v);
            reply(msg);
            return;
        }
        effects_set_bright_pct((uint8_t)v);
        (void)snprintf(msg, sizeof msg, "OK BRIGHT=%u%% (%u/%u)  CTRL=%s  KEY3=%s",
                       (unsigned)v, (unsigned)g_bright, (unsigned)DUTY_MAX,
                       ctrl_str(), effects_pot_name());
        reply(msg);
        return;
    }

    /* ---- SPEED<n> ---- */
    if (strncmp(line, "SPEED", 5u) == 0) {
        if (parse_u16(line + 5, &v) == 0u) {
            reply("ERR SPEED needs a number 0..100  (e.g. SPEED30)");
            return;
        }
        if (v > 100u) {
            (void)snprintf(msg, sizeof msg, "ERR SPEED=%u out of range 0..100", (unsigned)v);
            reply(msg);
            return;
        }
        effects_set_speed_pct((uint8_t)v);
        (void)snprintf(msg, sizeof msg, "OK SPEED=%u%% (%u)  CTRL=%s  KEY3=%s",
                       (unsigned)v, (unsigned)g_speed,
                       ctrl_str(), effects_pot_name());
        reply(msg);
        return;
    }

    /* ---- TLM<0|1> ---- */
    if (strncmp(line, "TLM", 3u) == 0) {
        if (parse_u16(line + 3, &v) == 0u || v > 1u) {
            reply("ERR TLM needs 0 or 1  (0=quiet default, 1=10Hz frames)");
            return;
        }
        telemetry_set_periodic((uint8_t)v);
        (void)snprintf(msg, sizeof msg, "OK TLM=%u  (%s)", (unsigned)v,
                       (v != 0u) ? "10Hz frames ON" : "quiet");
        reply(msg);
        return;
    }

    /* ---- HELP / ? ---- */
    if (strcmp(line, "HELP") == 0 || strcmp(line, "?") == 0) {
        reply("MODE<n> n=1..5 | BRIGHT<n> n=0..100 | SPEED<n> n=0..100");
        reply("ASK MODE | ASK BRIGHT | ASK SPEED | TLM<0|1> | HELP");
        reply("CTRL=POT: pot is the active controller (manual) | CTRL=HOLD: value held");
        return;
    }

    /* ---- 其它：报未知，并把合法指令直接列出来 ---- */
    (void)snprintf(msg, sizeof msg,
        "ERR unknown '%s' | valid: MODE1-5 BRIGHT0-100 SPEED0-100 TLM0/1 ASK HELP", line);
    reply(msg);
}

/*---------------------------------------------------------------------------*/
void cli_init(void)
{
    s_len       = 0u;
    s_ovf       = 0u;
    s_ready     = 0u;
    s_bad       = 0u;
    s_rearm     = 0u;
    s_last_tick = 0u;
    s_build[0]  = '\0';
    s_rdy[0]    = '\0';

    if (HAL_UARTEx_ReceiveToIdle_IT(&huart1, s_rx, (uint16_t)sizeof s_rx) != HAL_OK) {
        s_rearm = 1u;           /* 起不来就让 cli_tick 重试 */
    }
}

void cli_tick(void)
{
    /* 接收没武装起来（比如上一次出错），重试 */
    if (s_rearm != 0u) {
        if (HAL_UARTEx_ReceiveToIdle_IT(&huart1, s_rx, (uint16_t)sizeof s_rx) == HAL_OK) {
            s_rearm = 0u;
        }
    }

    /* 没有换行符的情况：线上静了 200ms 就把攒的当成一条完整指令 */
    if (s_ready == 0u && (s_len > 0u || s_ovf != 0u)) {
        if ((g_app_tick - s_last_tick) >= (uint32_t)CLI_QUIET_TICKS) {
            cli_commit();
        }
    }

    if (s_ready == 0u) {
        return;
    }

    if (s_bad != 0u) {
        s_bad   = 0u;
        s_ready = 0u;
        reply("ERR line too long (max 23 chars)");
        return;
    }

    s_ready = 0u;
    cli_exec(s_rdy);
}

/*---------------------------------------------------------------------------*/
/* 中断回调                                                                   */

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    uint16_t i;

    if (huart->Instance != USART1) {
        return;
    }

    for (i = 0u; i < Size; i++) {
        cli_push((char)s_rx[i]);
    }
    s_last_tick = g_app_tick;

    /* 重新武装。HAL 在调用本回调前已把 RxState 置回 READY，可以安全重入。 */
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart1, s_rx, (uint16_t)sizeof s_rx) != HAL_OK) {
        s_rearm = 1u;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) {
        return;
    }

    /* 发送侧：把忙标志解锁，否则遥测会永久静默 */
    telemetry_tx_reset();

    /* 接收侧：出错后 HAL 会停止接收，必须重新武装，否则串口再也收不到东西 */
    s_len = 0u;
    s_ovf = 0u;
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart1, s_rx, (uint16_t)sizeof s_rx) != HAL_OK) {
        s_rearm = 1u;
    }
}
