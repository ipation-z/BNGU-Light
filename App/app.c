/**
  ******************************************************************************
  * @file    app.c
  * @brief   应用调度：定时器回调分派 + 100Hz 节拍处理
  ******************************************************************************
  */
#include "main.h"
#include "app_config.h"
#include "app.h"
#include "led_matrix.h"
#include "adc.h"
#include "telemetry.h"
#include "keys.h"
#include "effects.h"

volatile uint8_t  g_mode = MODE_WALK;      /* effects_init() 会重新设置 */
volatile uint32_t g_app_tick;

static uint32_t s_last_tick;
static uint8_t  s_tlm_div;

/*---------------------------------------------------------------------------*/
void app_init(void)
{
    led_matrix_init();      /* 先把帧数据清零、栅极置安全态 */
    adc_init();             /* ADC 校准 + 启动 DMA */
    telemetry_init();       /* 开机横幅 */
    keys_init();            /* KEY2/KEY3 去抖状态 */
    effects_init();         /* 进入开机动画（MODE5 只跑一轮，然后进 MODE1） */

    /* 启动顺序：
     *   TIM3 先跑 —— 它既是应用节拍，又通过 TRGO 触发 ADC 采样（必须在
     *               HAL_ADC_Start_DMA 之后启动，否则第一次触发会丢）
     *   TIM2 再跑 —— 20kHz 阴极 PWM
     *   TIM1 最后 —— 1kHz 槽切换；它一到就会把 TIM2 的计数器清零做相位锁定
     */
    (void)HAL_TIM_Base_Start_IT(&htim3);
    (void)HAL_TIM_Base_Start_IT(&htim2);
    (void)HAL_TIM_Base_Start_IT(&htim1);
}

/*---------------------------------------------------------------------------*/
void app_tick(void)
{
    adc_update();          /* 读 DMA 缓冲、换算 mV、轻滤波 */
    keys_tick();           /* KEY2/KEY3 采样与去抖 */
    effects_tick();        /* 电位器解析 + 模式推进 */

    if (++s_tlm_div >= TLM_PERIOD_TICKS) {
        s_tlm_div = 0u;
        telemetry_tick();  /* 10Hz 遥测帧 */
    }
}

/*---------------------------------------------------------------------------*/
void app_run(void)
{
    if (s_last_tick == g_app_tick) {
        return;            /* 没有新节拍 */
    }
    s_last_tick = g_app_tick;
    app_tick();
}

/*---------------------------------------------------------------------------*/
/* 三个定时器共用这一个回调 —— 必须按 Instance 分派！                        */
/* 不判断的话 TIM1/TIM3 也会去跑 PWM 代码，表现为亮度乱跳、槽错乱。          */
/*---------------------------------------------------------------------------*/
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) {
        led_matrix_slot_isr();     /* 1kHz  槽切换 */
    } else if (htim->Instance == TIM2) {
        led_matrix_pwm_isr();      /* 20kHz 阴极 PWM */
    } else if (htim->Instance == TIM3) {
        g_app_tick++;              /* 100Hz 应用节拍 */
    }
}
