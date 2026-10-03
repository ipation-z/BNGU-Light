/**
  ******************************************************************************
  * @file    display_data.c
  ******************************************************************************
  */
#include "display_data.h"

const uint8_t g_order[LED_CHAR_COUNT][LED_DOT_PER_CHAR] = {
    /* B */ {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u},
    /* N */ {0u, 1u, 2u, 3u, 4u, 7u, 6u, 5u},   /* 第 6 灯与第 8 灯互换 */
    /* G */ {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u},
    /* U */ {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u}
};

const char *const g_char_name[LED_CHAR_COUNT] = {"B", "N", "G", "U"};
