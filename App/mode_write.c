/**
  ******************************************************************************
  * @file    mode_write.c
  * @brief   MODE1 累计书写 + 逆序擦除（带渐亮 / 渐灭）
  *
  *  写：一条"笔尖"沿 32 个点前进，走过的地方累积点亮（权重满档）；
  *      笔尖所在的那个点按 0→满档 渐亮，所以看起来是"写"出来的，
  *      而不是一格一格硬跳。
  *  擦：笔尖回到起点，沿同样顺序前进，被扫过的点按 满档→0 渐灭。
  *
  *  实现方式：维护一个连续的位置 s_pos（单位 1/POS_PER_DOT 个点），
  *  每个点的亮度 = 它落后笔尖多远 的线性斜坡，和 MODE5 的彗尾头部同一套思路。
  *  区别是 MODE5 头部后面会衰减回 0（彗尾），这里饱和后就一直保持满档（写字）。
  *
  *  节奏：每前进 1 个点用 g_speed × MODE1_SPEED_MUL 个 tick，和以前一致。
  ******************************************************************************
  */
#include "main.h"
#include "app_config.h"
#include "led_matrix.h"
#include "display_data.h"
#include "telemetry.h"
#include "effects.h"
#include "modes.h"

typedef enum {
    W_WRITE = 0,
    W_HOLD_ON,
    W_ERASE,
    W_HOLD_OFF
} w_state_t;

/* 渐亮/渐灭跨多少个"子位置" */
#define FADE_SPAN   ((uint16_t)(MODE1_FADE_DOTS * POS_PER_DOT))
/* 笔尖走完全程的位置：最后一个点也完全饱和 */
#define WIPE_END    ((uint16_t)(((LED_DOT_TOTAL - 1u) * POS_PER_DOT) + FADE_SPAN))

static w_state_t s_st;
static uint16_t  s_pos;      /* 笔尖位置，单位 1/POS_PER_DOT 点 */
static uint16_t  s_acc;      /* 位置累加器 */
static uint16_t  s_cnt;

static uint16_t step_ticks(void)
{
    uint16_t t = (uint16_t)g_speed * MODE1_SPEED_MUL;
    return (t == 0u) ? 1u : t;
}

/* writing = 1：笔尖之后的点是暗的（正在写）
   writing = 0：笔尖之后的点是亮的（正在擦） */
static void render(uint8_t writing, uint16_t pos)
{
    uint8_t  wt[LED_CHAR_COUNT][LED_DOT_PER_CHAR];
    uint8_t  cc, i;
    uint16_t kk;

    /* 先清空（即使 g_order 不是完美置换也不会残留） */
    for (cc = 0u; cc < LED_CHAR_COUNT; cc++) {
        for (i = 0u; i < LED_DOT_PER_CHAR; i++) {
            wt[cc][i] = 0u;
        }
    }

    for (kk = 0u; kk < LED_DOT_TOTAL; kk++) {
        uint16_t base = (uint16_t)(kk * POS_PER_DOT);   /* 该点在路径上的位置 */
        uint8_t  v;

        if (pos <= base) {
            /* 笔尖还没到这个点 */
            v = (writing != 0u) ? 0u : (uint8_t)WEIGHT_LEVELS;
        } else {
            uint16_t d = (uint16_t)(pos - base);        /* 落后笔尖多远 */
            if (d >= FADE_SPAN) {
                /* 已经走过去了 */
                v = (writing != 0u) ? (uint8_t)WEIGHT_LEVELS : 0u;
            } else {
                /* 正在过渡：写是 0→满档，擦是 满档→0 */
                uint8_t r = (uint8_t)(((uint32_t)d * WEIGHT_LEVELS) / FADE_SPAN);
                v = (writing != 0u) ? r : (uint8_t)(WEIGHT_LEVELS - r);
            }
        }

        cc = (uint8_t)(kk / LED_DOT_PER_CHAR);
        i  = (uint8_t)(kk % LED_DOT_PER_CHAR);
        wt[cc][g_order[cc][i]] = v;
    }

    led_matrix_publish(wt);
}

/* 推进笔尖；返回 1 表示走到头了 */
static uint8_t advance(void)
{
    uint16_t sp = step_ticks();

    s_acc = (uint16_t)(s_acc + POS_PER_DOT);
    while (s_acc >= sp) {
        s_acc = (uint16_t)(s_acc - sp);
        s_pos++;
        if (s_pos >= WIPE_END) {
            s_pos = WIPE_END;
            return 1u;
        }
    }
    return 0u;
}

/*---------------------------------------------------------------------------*/
void mode_write_init(void)
{
    s_st  = W_WRITE;
    s_pos = 0u;
    s_acc = 0u;
    s_cnt = 0u;
    render(1u, 0u);
    telemetry_line("M1", "write start");
}

void mode_write_step(void)
{
    switch (s_st) {

    /* 写：笔尖前进，走过的地方累积点亮，笔尖处渐亮 */
    case W_WRITE:
        if (advance() != 0u) {
            s_st  = W_HOLD_ON;
            s_cnt = 0u;
            telemetry_line("M1", "all on");
        }
        render(1u, s_pos);
        break;

    case W_HOLD_ON:
        if (++s_cnt >= MODE1_HOLD_ON_TICKS) {
            s_cnt = 0u;
            s_st  = W_ERASE;
            s_pos = 0u;
            s_acc = 0u;
            render(0u, 0u);
            telemetry_line("M1", "erase start");
        }
        break;

    /* 擦：笔尖从起点再走一遍，被扫过的点渐灭 */
    case W_ERASE:
        if (advance() != 0u) {
            s_st  = W_HOLD_OFF;
            s_cnt = 0u;
            telemetry_line("M1", "all off");
        }
        render(0u, s_pos);
        break;

    case W_HOLD_OFF:
        if (++s_cnt >= MODE1_HOLD_OFF_TICKS) {
            s_cnt = 0u;
            s_st  = W_WRITE;
            s_pos = 0u;
            s_acc = 0u;
            render(1u, 0u);
            telemetry_line("M1", "write start");
        }
        break;

    default:
        s_st  = W_WRITE;
        s_cnt = 0u;
        break;
    }
}
