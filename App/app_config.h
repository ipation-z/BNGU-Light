/**
  ******************************************************************************
  * @file    app_config.h
  * @brief   BNGU_Light 所有可调参数的唯一来源（改参数只改这里）
  ******************************************************************************
  */
#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/* ============================ 扫描引擎 ============================ */
#define LED_CHAR_COUNT        4u      /* B / N / G / U */
#define LED_DOT_PER_CHAR      8u      /* 每个字符 8 颗灯（K1~K8） */
#define LED_DOT_TOTAL         32u     /* 4 × 8 */

/* 一个字符槽 = 1ms = TIM2 的 20 个周期（TIM2 ARR=49 → 50µs） */
#define PWM_TICKS_PER_SLOT    20u

/* 槽起点的消隐 tick 数：
 *   1 → Vgs 在 50µs 时约 -0.56V，低于 AO3401 阈值 -0.9V，一般看不到鬼影
 *   2 → 100µs，Vgs 约 -0.09V，完全关断，但亮度上限降到 18
 * 上板若看到前一个字符残影，就把它改成 2。 */
#define BLANK_TICKS           1u
#define DUTY_MAX              (PWM_TICKS_PER_SLOT - BLANK_TICKS)   /* 19 */

/* 对外默认亮度（0..DUTY_MAX）。Phase A 自检会临时用满 DUTY_MAX */
#define DUTY_DEFAULT          15u

/* 每个槽起点把 TIM2->CNT 清零做相位锁定（见 led_matrix.c） */

/* ---------------------------------------------------------------------------
 * 点的亮度权重：把 1ms 的槽再切成 WEIGHT_LEVELS 个子时段，
 * 每个点可以设置 0..WEIGHT_LEVELS 的权重，表示它在多少个连续子时段里点亮。
 * 于是同一个字符内部也能做出 8 级明暗（PWM 在共用阴极上做不到分点调光）。
 *   权重 8 = 100%，4 = 50%，1 = 12.5%，0 = 不亮
 * 全局亮度仍然由 duty 控制，两者相乘。
 * ------------------------------------------------------------------------- */
#define WEIGHT_LEVELS         8u

/* ============================ 应用节拍 ============================ */
#define APP_TICK_HZ           100u    /* TIM3 = 100Hz */

/* ============================ ADC ============================ */
#define ADC_CH_KEY            0u      /* DMA 缓冲[0] = CH0 = PA0 = 电源键检测 */
#define ADC_CH_POT            1u      /* DMA 缓冲[1] = CH1 = PA1 = 电位器   */
#define ADC_VREF_MV           3300u
#define ADC_FULL_SCALE        4095u

/* ============================ 串口遥测 ============================ */
#define TLM_PERIOD_TICKS      10u     /* 100Hz / 10 = 10Hz */
#define TLM_BUF_SIZE          128u

/* ============================ MODE5 描边游走（兼开机动画）============================ */
/* 节奏：1 tick = 10ms（来自 TIM3 的 100Hz 应用节拍）
 *
 *  走速不在这里 —— 由全局速度参数 g_speed 控制（见下面的 SPEED_* 和电位器）：
 *      g_speed = 12 → 120ms/点（默认手感）
 *      g_speed = 25 → 250ms/点（慢，适合逐点检查焊接）
 *      g_speed =  2 → 20ms/点 （最快）
 *
 *  SELFTEST_HOLD_ON_TICKS   全亮保持多久（演示/拍照用）
 *  SELFTEST_FADE_TICKS      渐亮 / 渐灭的步长：每多少 tick 变 1 级亮度
 *                           2 → 19 级 × 20ms ≈ 0.38s
 *  SELFTEST_HOLD_OFF_TICKS  全灭保持多久
 *
 *  平滑彗尾：亮点不是"一格一格跳"，而是一个连续的三角亮度分布滑过 32 个点。
 *    POS_PER_DOT        每个点细分成多少个位置（越大越平滑，CPU 略增）
 *    → 走速由全局速度参数 g_speed（tick 数/点）决定：
 *        g_speed = POS_PER_DOT(12) 时正好 120ms/点，也就是现在的手感
 *    COMET_LEAD  头部前方渐亮跨几个点（0 = 头部直接跳到满亮，会有"啪"的跳变）
 *    COMET_TAIL  头部后方渐灭跨几个点（越大拖尾越长）
 */
#define SELFTEST_HOLD_ON_TICKS    70u   /* 0.7s 全亮 */
#define SELFTEST_FADE_TICKS        2u   /* 每 20ms 变 1 级 → 约 0.38s 渐亮/渐灭 */
#define SELFTEST_HOLD_OFF_TICKS   30u   /* 0.3s 全灭 */

#define POS_PER_DOT               12u   /* 每个点细分成 12 个位置 */
#define COMET_LEAD                 1u   /* 头部前方 1 个点的渐亮 */
#define COMET_TAIL                 3u   /* 头部后方 3 个点的渐灭 */

/* ============================ 模式号 ============================ */
/* MODE3 / MODE4 是 Phase C / Phase D 的内容，先占号不实现 */
#define MODE_COUNT                 6u
#define MODE_WRITE                 1u   /* 累计书写 + 逆序擦除 */
#define MODE_FLOW                  2u   /* 字符流水 */
#define MODE_BREATH                3u   /* 同步呼吸（Phase C） */
#define MODE_MIX                   4u   /* 流水 + 呼吸（Phase D） */
#define MODE_WALK                  5u   /* 描边游走（也用作开机动画） */

/* ============================ 按键 ============================ */
#define KEY_DEBOUNCE_TICKS         3u   /* 3 × 10ms = 30ms 去抖 */

/* 切换模式时的闪灯提示：闪"模式号"次（MODE1 闪 1 下、MODE5 闪 5 下）
   一下 = FLASH_ON + FLASH_OFF = 10 + 7 = 17 tick = 170ms
   所以整段提示时长：MODE1 0.17s / MODE2 0.34s / MODE5 0.85s
   想让闪得更慢就加大这两个值，想更快就减小。 */
#define FLASH_ON_TICKS            10u   /* 每次亮 100ms */
#define FLASH_OFF_TICKS            7u   /* 每次灭  70ms（就是"间隔"，比原来 60ms 略长） */

/* ============================ 电位器 ============================ */
/* 上板后用万用表量一下电位器两端实际 mV，填进来可以做满量程标定 */
#define POT_MV_MIN               100u
#define POT_MV_MAX              3200u

/* 电位器拧到最左时的最低亮度档：不要给 1 —— 1 级时每颗灯平均只有 0.15mA，
   亮房间里看着就是黑的。4 级约 0.6mA，任何光线下都看得见。 */
#define BRIGHT_MIN                 4u

/* 软接管：切换 KEY3 档位（或刚上电 / 切模式）时先把参数冻结住，
   等电位器真的转过这么多百分比才重新跟手 —— 避免参数突然跳变。
   5% 也是为了滤掉 ADC 的抖动。 */
#define POT_TAKEOVER_PCT           5u

#define POT_TARGET_BRIGHT          0u   /* 电位器只调亮度峰值 */
#define POT_TARGET_SPEED           1u   /* 电位器只调速度 */
#define POT_TARGET_BOTH            2u   /* 联动（默认） */
#define POT_TARGET_COUNT           3u

/* ============================ 全局参数默认/范围 ============================ */
/* 速度单位：每多少 tick（10ms）推进一步，越小越快 */
#define SPEED_MIN                  2u   /* 20ms/步   最快 */
#define SPEED_MAX                 50u   /* 500ms/步  最慢 */
#define SPEED_DEFAULT             12u   /* 120ms/步 */

/* 各模式的速度倍率：让同一个速度参数在每个模式里都"看起来合适" */
#define MODE1_SPEED_MUL            1u   /* 每走一个点 */
#define MODE2_SPEED_MUL            3u   /* 每个字母保持 3 倍时长 */

/* MODE1 的保持时间 */
#define MODE1_HOLD_ON_TICKS       30u   /* 全亮保持 0.3s */
#define MODE1_HOLD_OFF_TICKS      20u   /* 全灭保持 0.2s */

/* MODE1 的渐亮 / 渐灭：过渡跨几个点。
   1 = 和 MODE5 彗尾头部同样的柔度；0 会变成"一格一格硬跳"。
   注意它不影响节奏 —— 每点亮/熄灭一个点仍然用 g_speed 个 tick。 */
#define MODE1_FADE_DOTS            1u

/* ==================== 呼吸包络（MODE3 同步呼吸 / MODE4 流水+呼吸 共用）==================== */
/* 包络是 64 相位的半正弦（见 App/breath.c）。
   每个相位持续 g_speed / XXX_SPEED_DIV 个 tick：
     MODE3 用 BREATH_SPEED_DIV，一轮同步呼吸 ≈ 4.2s
     MODE4 用 MODE4_SPEED_DIV，每个字母一次呼吸 ≈ 2.1s（比 MODE3 快一倍，
           否则 4 个字母轮流下来要 17 秒，演示时太拖） */
#define BREATH_STEPS              64u
#define BREATH_SPEED_DIV           2u
#define MODE4_SPEED_DIV            4u

/* 呼吸的停顿：最低点停 BREATH_DARK_HOLD_PHASES 个相位，最高点停
   BREATH_BRIGHT_HOLD_PHASES 个相位 —— 就是"吸-停-呼-停"的节奏。
   两个值保持相等，上升段和下降段的总时长才对称；只在下端停的话，
   眼睛会把"灭着不动"算进熄灭过程，看起来就是"变亮比熄灭快"。
   0 = 不停（连续起伏）。 */
#define BREATH_DARK_HOLD_PHASES    3u
#define BREATH_BRIGHT_HOLD_PHASES  3u

/* MODE3 的最低亮度档（MODE4 固定用 0，因为字母之间要真的黑掉才好分辨）。
   0 = 最低点真的断电（duty 0，完全熄灭）。代价：duty 0↔1 在感知上是 0↔38%
       的跳变（LED 在 1/19 占空比时就挺亮了），所以熄灭/点亮各会有一下"啪"。
       不过 duty 1 时每颗灯平均只有 0.15mA，绝对值很低，加上极值点有停顿，
       实际观感就是"灯熄了、停一下、再亮起来"。
   1 = 最低点保留一点微光（0.15mA），底下那一步不会有跳变，但暗室里能看见残光。 */
#define MODE3_DUTY_MIN             0u

/* ==================== 一键开关机（Phase E）==================== */
/* ADC1(PA0) 上的按键电压：按下约 2.0~2.6V，未按下接地（≈0V）。
   阈值取 1.2V —— 离"按下"最低 2.0V 有 0.8V 余量，离"松开"0V 有 1.2V 余量。 */
#define PWR_KEY_MV_THRESHOLD    1200u

/* 上电后先忽略电源检测的节拍数（30 次 = 300ms）。
   为什么要它：ADC 的 DMA 缓冲上电时是 0，第一次转换要等 TIM3 的 TRGO，
   所以最开始的读数必然是"0V = 未按下"这种假低电平。如果不屏蔽掉，
   再加上"连续 3 次低电平就算松开"，就有可能在上电瞬间误判成"按键已松开"
   → 进入 ARMED → 紧接着读到真正的高电平 → 300ms 后自己关机。
   屏蔽 300ms 后 ADC 和 3.3V 轨都已稳定，判断才可信。 */
#define PWR_STARTUP_IGNORE_TICKS   30u

/* 上电后要连续读到这么多次"松开"才算真的松开（3 次 = 30ms 去抖）。
   这一步是"按下上电不会立刻又关机"的关键：不等到松开，绝不允许检测关机。 */
#define PWR_RELEASE_CONFIRM        3u

/* 松开检测超时：上电后 5s 还是读到高电平（按键卡住 / ADC1 还没接），
   就彻底放弃关机检测并打日志。
   宁可关不掉（拔电就行），也绝不能一开机就自己关机。 */
#define PWR_RELEASE_TIMEOUT      500u

/* 要连续按住这么多次才执行关机（30 次 = 300ms）。
   就是普通"按一下"的时长，同时挡住抖动和误触。 */
#define PWR_OFF_HOLD_TICKS         30u

#endif /* __APP_CONFIG_H */
