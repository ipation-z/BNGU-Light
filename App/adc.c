/**
  ******************************************************************************
  * @file    adc.c
  ******************************************************************************
  */
#include "main.h"
#include "app_config.h"
#include "adc.h"

/* DMA 目标缓冲：必须是 uint16_t（DMA 配置为 HALFWORD），长度 = 通道数 2 */
static uint16_t s_raw[2];
static uint16_t s_mv[2];
static uint8_t  s_fill[2];

void adc_init(void)
{
    s_raw[0] = 0u;  s_raw[1] = 0u;
    s_mv[0]  = 0u;  s_mv[1]  = 0u;
    s_fill[0] = 0u; s_fill[1] = 0u;

    /* 必须在 HAL_ADC_Start_DMA 之前校准，否则偏差可达十几 mV */
    (void)HAL_ADCEx_Calibration_Start(&hadc1);

    /* 循环 DMA，长度 = 2 个转换（不是字节数） */
    (void)HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_raw, 2u);
}

void adc_update(void)
{
    uint8_t i;
    for (i = 0u; i < 2u; i++) {
        uint32_t mv = ((uint32_t)s_raw[i] * ADC_VREF_MV + (ADC_FULL_SCALE / 2u))
                      / ADC_FULL_SCALE;
        if (s_fill[i] < 4u) {
            s_mv[i] = (uint16_t)mv;          /* 前 4 次直接取值，快速建立 */
            s_fill[i]++;
        } else {
            s_mv[i] = (uint16_t)(((uint32_t)s_mv[i] * 3u + mv) / 4u);  /* 一阶低通 */
        }
    }
}

uint16_t adc_mv(uint8_t ch)
{
    return (ch < 2u) ? s_mv[ch] : 0u;
}

uint16_t adc_raw(uint8_t ch)
{
    return (ch < 2u) ? s_raw[ch] : 0u;
}
