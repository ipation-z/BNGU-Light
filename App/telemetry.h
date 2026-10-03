/**
  ******************************************************************************
  * @file    telemetry.h
  * @brief   串口遥测：10Hz 发送模式 / 亮度 / 速度 / ADC1 / ADC2
  *
  *  全部通过 DMA 发送，绝不在中断或主循环里阻塞等待。
  *  电压一律用整数 mV —— 链接用 nano.specs 且未开 _printf_float，
  *  printf("%f") 会静默输出空字符串。
  ******************************************************************************
  */
#ifndef __TELEMETRY_H
#define __TELEMETRY_H

void telemetry_init(void);                          /* 发开机横幅 */
void telemetry_tick(void);                          /* 10Hz：发 [TLM] 帧 */
void telemetry_line(const char *tag, const char *msg);  /* 发事件行 [TAG ] msg */

#endif /* __TELEMETRY_H */
