/**
  ******************************************************************************
  * @file    telemetry.h
  * @brief   串口输出：事件行 / 10Hz 周期帧 / 串口指令回包队列
  *
  *  全部通过 DMA 发送，绝不在中断或主循环里阻塞等待。
  *  电压一律用整数 mV —— 链接用 nano.specs 且未开 _printf_float，
  *  printf("%f") 会静默输出空字符串。
  ******************************************************************************
  */
#ifndef __TELEMETRY_H
#define __TELEMETRY_H

#include <stdint.h>

void telemetry_init(void);                          /* 发开机横幅 + 指令提示 */

/* 10Hz 周期帧。**默认关闭**（蓝牙/串口界面上满屏滚很烦），
   发 TLM1 打开、TLM0 关闭。*/
void telemetry_tick(void);

/* 立即发一行 [TAG ] msg。发送忙时直接丢弃，绝不阻塞。 */
void telemetry_line(const char *tag, const char *msg);

/* 把若干行排进发送队列，由 telemetry_pump() 每 10ms 一行发出去。
   一次回多行（比如 HELP）时用它，避免被忙标志吃掉后面的行。 */
void telemetry_queue(const char *tag, const char *msg);

/* 100Hz 调用：队列里有一行、且发送空闲，就发出去 */
void telemetry_pump(void);

/* 串口出错后由 cli.c 调用，清掉"发送忙"标志免得遥测永久静默 */
void telemetry_tx_reset(void);

/* 1 = 发送空闲（可以立即发下一行） */
uint8_t telemetry_tx_idle(void);

/* 10Hz 周期帧开关 */
void    telemetry_set_periodic(uint8_t on);
uint8_t telemetry_periodic_is_on(void);

#endif /* __TELEMETRY_H */
