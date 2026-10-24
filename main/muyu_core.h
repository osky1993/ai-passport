// main/muyu_core.h —— “敲木鱼，攒功德”应用的纯逻辑状态机。
//
// 本模块不依赖 ESP-IDF / LVGL / FreeRTOS，只处理“按键 + 时间 → 动作”的决策，
// 由 tests/test_muyu_core.c 在主机上覆盖。设备侧的应用任务（muyu_app.c）
// 是本状态机唯一的所有者：所有函数都只能在该任务里串行调用，不需要加锁。
//
// 时间统一使用单调毫秒（设备上来自 esp_timer_get_time()/1000），调用方保证
// 不回退；所有超时判断都用差值比较，不依赖绝对时间起点。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// 时间参数（毫秒）。全部是交互/功耗的取舍值，不是硬件约束，可按真机体验调整。
// ---------------------------------------------------------------------------
#define MUYU_AUTO_MAX_MS          (10 * 60 * 1000)  // 自动敲击无人值守最长 10 分钟，之后自动停止以省电
#define MUYU_DIM_AFTER_MS         20000  // 无操作 20 s 调暗背光
#define MUYU_OFF_AFTER_MS         45000  // 无操作 45 s 关闭背光（屏幕内容保留）
#define MUYU_SLEEP_AFTER_MS       (3 * 60 * 1000)   // 无操作 3 min 且未自动敲击 → 请求 deep sleep
// 最后一次敲击后静默这么久再写 NVS：须大于敲击音时长（320 ms），保证写 Flash 时没有音效
// 在播放（Flash 写入会短暂关闭 cache，可能造成播放卡顿）。同时要小于自动一档的最大间隔
// （400 ms），否则一档连续自动敲击时永远等不到保存窗口。
#define MUYU_SAVE_QUIET_MS        360
#define MUYU_SAVE_MIN_GAP_MS      5000   // 两次 NVS 写入的最小间隔，限制 Flash 磨损

#define MUYU_MILESTONE_EVERY      108    // 每敲满 108 下（“百八烦恼”）给一次特别提示

// 档位数量：音量与自动敲击都是 0..3 共四档，0 分别表示静音 / 不自动。
#define MUYU_LEVELS               4

// 音量档位对应的 codec 音量百分比。esp_codec_dev 默认曲线把 0..100% 线性映射到
// -50..0 dB，ES8311 驱动再补偿板级增益 +3.6 dB（DAC 3.3 V / 功放 5 V）。敲击音 PCM
// 峰值为 -1 dBFS，所以三档取 codec 净增益 -12 / -6 / 0 dB，最响一档恰好不削波：
//   一档 69%：-50 + 69×0.5 = -15.5 dB，+3.6 → 约 -12 dB
//   二档 81%：-9.5 dB + 3.6 → 约 -6 dB
//   三档 93%：-3.5 dB + 3.6 → 约 0 dB
#define MUYU_VOLUME_PCT_1         69
#define MUYU_VOLUME_PCT_2         81
#define MUYU_VOLUME_PCT_3         93
#define MUYU_VOLUME_DEFAULT       2      // 首次开机的音量档

// 自动敲击各档的间隔范围（ms，闭区间）。每次在范围内随机取值，节奏更像人手敲。
#define MUYU_AUTO1_MIN_MS         200    // 一档：快
#define MUYU_AUTO1_MAX_MS         400
#define MUYU_AUTO2_MIN_MS         500    // 二档：中
#define MUYU_AUTO2_MAX_MS         800
#define MUYU_AUTO3_MIN_MS         1000   // 三档：慢，接近念经木鱼的节奏
#define MUYU_AUTO3_MAX_MS         1500

// 背光亮度档位（百分比），由 muyu_backlight_percent() 映射。
#define MUYU_BACKLIGHT_ACTIVE     100
#define MUYU_BACKLIGHT_DIM        15
#define MUYU_BACKLIGHT_OFF        0

// 屏幕电源状态：只描述背光，不代表芯片休眠。
typedef enum {
    MUYU_SCREEN_ON = 0,   // 正常亮度
    MUYU_SCREEN_DIM,      // 调暗，仍可看清；此时按 OK 直接敲击并恢复亮度
    MUYU_SCREEN_OFF,      // 背光关闭；此时任意按键只唤醒屏幕，不产生功能动作
} muyu_screen_t;

// 应用关心的按键输入。设备侧把 bsp_btn_t + bsp_btn_ev_t 翻译成这里的值，
// 使本模块无需包含 BSP 头文件。
typedef enum {
    MUYU_KEY_NONE = 0,    // 不关心的事件（双击、长按、抬起等）
    MUYU_KEY_STRIKE,      // OK 按下瞬间：敲木鱼（用 PRESS 而不是 CLICK，手感更跟手）
    MUYU_KEY_AUTO,        // UP 单击：自动敲击档位循环 不自动 → 一档 → 二档 → 三档 → 不自动
    MUYU_KEY_VOLUME,      // DOWN 单击：音量档位循环 静音 → 一档 → 二档 → 三档 → 静音
} muyu_key_t;

// 一次决策产生的动作位图。调用方逐位执行；多位可同时置位。
enum {
    MUYU_ACT_STRIKE      = 1u << 0,  // 计数已 +1：播放敲击动画、音效、飘字
    MUYU_ACT_MILESTONE   = 1u << 1,  // 本次敲击恰好凑满 MUYU_MILESTONE_EVERY（与 STRIKE 同时置位）
    MUYU_ACT_REFRESH     = 1u << 2,  // 状态标签（自动档/音量档）需要刷新
    MUYU_ACT_BACKLIGHT   = 1u << 3,  // 屏幕电源状态变化：按 muyu_backlight_percent() 重设背光
    MUYU_ACT_SAVE        = 1u << 4,  // 现在适合把功德与设置写入 NVS
    MUYU_ACT_SLEEP       = 1u << 5,  // 长时间无操作：保存后进入 deep sleep
};

// 应用全部可变状态。字段只由本模块函数修改；调用方可以只读访问以刷新 UI。
typedef struct {
    uint32_t total;            // 累计功德（跨重启，持久化到 NVS）；到 UINT32_MAX 后饱和不再回绕
    uint32_t session;          // 本次开机敲击次数（不持久化）；同样饱和
    uint8_t volume;            // 音量档 0..3，0 = 静音（持久化）
    uint8_t auto_level;        // 自动敲击档 0..3，0 = 不自动（不持久化，开机总是 0）
    muyu_screen_t screen;      // 当前背光状态

    int64_t last_input_ms;     // 最近一次用户按键时间：决定调暗/熄屏/休眠
    int64_t last_strike_ms;    // 最近一次敲击（手动或自动）时间：决定保存时机
    int64_t next_auto_ms;      // 自动模式下一次敲击的时间点
    int64_t auto_since_ms;     // 自动模式开启时间：用于 MUYU_AUTO_MAX_MS 限时
    int64_t last_save_ms;      // 最近一次发出 SAVE 的时间
    bool dirty;                // 有未保存的功德/设置改动
    bool sleep_requested;      // 已发出过 SLEEP，避免每个 tick 重复请求
    uint32_t rng;              // 自动间隔随机数状态（xorshift32，非 0）
} muyu_state_t;

// 初始化状态。total/volume 来自 NVS（读取失败时调用方传 0 / MUYU_VOLUME_DEFAULT；
// 越界的 volume 会被替换为默认档）。seed 用于自动间隔随机化，设备上取硬件随机数，
// 测试中取固定值；0 会被替换为非 0 常量。
// now_ms 作为“最近一次操作”的起点，所以开机后会完整走一遍调暗/熄屏计时。
void muyu_init(muyu_state_t *s, uint32_t total, uint8_t volume, uint32_t seed, int64_t now_ms);

// 处理一次按键，返回 MUYU_ACT_* 位图。
// 熄屏时任意有效按键只点亮屏幕（返回 BACKLIGHT），避免在看不见界面时误操作。
uint32_t muyu_handle_key(muyu_state_t *s, muyu_key_t key, int64_t now_ms);

// 周期调用（建议 ≤ 100 ms 一次）：推进自动敲击、背光状态机、保存与休眠决策。
uint32_t muyu_tick(muyu_state_t *s, int64_t now_ms);

// 调用方完成 NVS 写入后调用。ok=false 时保留 dirty，下个保存窗口重试。
void muyu_mark_saved(muyu_state_t *s, bool ok, int64_t now_ms);

// 休眠请求没能执行（例如按键正被按住）时调用：retry_ms 后再次发出 SLEEP，
// 期间屏幕状态不变。
void muyu_sleep_deferred(muyu_state_t *s, int64_t now_ms, int64_t retry_ms);

// 音量档对应的 codec 音量百分比；0 档（静音）返回 0，越界按 0 处理。
uint8_t muyu_volume_percent(uint8_t volume);

// 从 *rng 取一个自动敲击间隔（ms），落在 level 档的闭区间内，并推进 *rng。
// level 为 0 或越界时返回 0。
uint32_t muyu_auto_interval_ms(uint8_t level, uint32_t *rng);

// 当前屏幕状态对应的背光百分比。
uint8_t muyu_backlight_percent(muyu_screen_t screen);

// 累计功德位数太多（≥10 位）时 44 px 数字在 224 px 宽度内放不下，需要换小号字体。
bool muyu_total_needs_small_font(uint32_t total);

// 本次敲击后应该飘出的文字（UTF-8 常量字符串，生命周期为整个程序）。
// milestone 为 true 时返回特别提示，否则返回普通的“功德 +1”。
const char *muyu_float_text(bool milestone);

// ---------------------------------------------------------------------------
// 木槌几何：纯计算，供 UI 动画与主机测试共用。
// 木槌是一条以 (pivot_x, pivot_y) 为握点、长 MUYU_MALLET_LEN 的直线，末端是槌头。
// progress 取 0..1000：0 = 抬起静止位，1000 = 敲到木鱼上。
// ---------------------------------------------------------------------------
#define MUYU_MALLET_PIVOT_X   224   // 握点 x（屏幕坐标，右下角附近，避开 30 px 圆角）
#define MUYU_MALLET_PIVOT_Y   250   // 握点 y
#define MUYU_MALLET_LEN       124   // 握点到槌头中心的距离（px）
// 角度按屏幕坐标系（y 向下）计：x = 握点x + L·cosθ，y = 握点y + L·sinθ；
// θ 越接近 270° 槌头越高。两个端点角度由木鱼外形推算，使敲击位槌头恰好贴住木鱼顶部。
#define MUYU_MALLET_REST_DEG  250   // 静止角度：槌头抬在木鱼右上方
#define MUYU_MALLET_HIT_DEG   222   // 敲击角度：槌头落在木鱼顶部偏右

// 计算槌头中心坐标。progress 超出 0..1000 时会被钳位。
void muyu_mallet_head(int progress, int *x, int *y);
