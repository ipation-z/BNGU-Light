/**
  ******************************************************************************
  * @file    breath.c
  ******************************************************************************
  */
#include "breath.h"

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

uint8_t breath_duty(uint8_t phase, uint8_t peak, uint8_t dmin)
{
    uint16_t span;
    uint16_t x;

    if (peak <= dmin) {
        return peak;
    }
    span = (uint16_t)(peak - dmin);
    x    = ((uint16_t)s_env[phase] * span) + 127u;   /* 四舍五入 */
    return (uint8_t)((uint16_t)dmin + (x / 255u));
}
