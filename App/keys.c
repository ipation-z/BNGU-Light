/**
  ******************************************************************************
  * @file    keys.c
  ******************************************************************************
  */
#include "main.h"
#include "app_config.h"
#include "keys.h"

typedef struct {
    uint16_t pin;
    uint8_t  stable;    /* 稳定后的电平：1 = 松开，0 = 按下 */
    uint8_t  cnt;       /* 与稳定值不一致的连续次数 */
} key_state_t;

static key_state_t s_key[2] = {
    { KEY2_Pin, 1u, 0u },
    { KEY3_Pin, 1u, 0u }
};

static volatile uint8_t s_events;

void keys_init(void)
{
    uint8_t i;
    for (i = 0u; i < 2u; i++) {
        s_key[i].stable = 1u;
        s_key[i].cnt    = 0u;
    }
    s_events = KEY_EV_NONE;
}

void keys_tick(void)
{
    uint8_t i;

    for (i = 0u; i < 2u; i++) {
        uint8_t raw = (HAL_GPIO_ReadPin(GPIOB, s_key[i].pin) == GPIO_PIN_SET) ? 1u : 0u;

        if (raw != s_key[i].stable) {
            if (s_key[i].cnt < 255u) {
                s_key[i].cnt++;
            }
            if (s_key[i].cnt >= KEY_DEBOUNCE_TICKS) {
                s_key[i].cnt    = 0u;
                s_key[i].stable = raw;
                if (raw == 0u) {          /* 下降沿 = 按下 */
                    s_events |= (i == 0u) ? KEY_EV_KEY2 : KEY_EV_KEY3;
                }
            }
        } else {
            s_key[i].cnt = 0u;
        }
    }
}

uint8_t keys_take_event(void)
{
    uint8_t e = s_events;
    s_events = KEY_EV_NONE;
    return e;
}
