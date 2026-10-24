// main/muyu_core.c —— “敲木鱼，攒功德”纯逻辑状态机实现。说明见 muyu_core.h。
#include "muyu_core.h"

#include <math.h>
#include <stddef.h>

// C11 严格模式下 math.h 不保证提供 M_PI，这里自带一个。
#define MUYU_PI 3.14159265358979f

// 计数 +1，到 UINT32_MAX 饱和。功德只增不减，回绕成 0 会让用户“功德清零”，不可接受。
static uint32_t saturating_inc(uint32_t value)
{
    return value == UINT32_MAX ? value : value + 1u;
}

// 执行一次敲击（手动或自动共用）：更新计数、标记待保存，返回 STRIKE（可能附带 MILESTONE）。
static uint32_t do_strike(muyu_state_t *s, int64_t now_ms)
{
    s->total = saturating_inc(s->total);
    s->session = saturating_inc(s->session);
    s->dirty = true;
    s->last_strike_ms = now_ms;

    uint32_t act = MUYU_ACT_STRIKE;
    // 里程碑按“本次开机”计，避免累计值很大时用户要敲很久才看到第一次提示。
    if (s->session % MUYU_MILESTONE_EVERY == 0) act |= MUYU_ACT_MILESTONE;
    return act;
}

// xorshift32：极小的伪随机数发生器，只用于让自动敲击节奏有自然的快慢起伏，
// 不用于任何安全用途。状态不能为 0（为 0 时会一直输出 0）。
static uint32_t next_random(uint32_t *rng)
{
    uint32_t x = *rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *rng = x;
    return x;
}

uint32_t muyu_auto_interval_ms(uint8_t level, uint32_t *rng)
{
    static const uint16_t RANGE[MUYU_LEVELS][2] = {
        { 0, 0 },
        { MUYU_AUTO1_MIN_MS, MUYU_AUTO1_MAX_MS },
        { MUYU_AUTO2_MIN_MS, MUYU_AUTO2_MAX_MS },
        { MUYU_AUTO3_MIN_MS, MUYU_AUTO3_MAX_MS },
    };
    if (level == 0 || level >= MUYU_LEVELS || rng == NULL) return 0;
    const uint32_t lo = RANGE[level][0], hi = RANGE[level][1];
    return lo + next_random(rng) % (hi - lo + 1u);
}

uint8_t muyu_volume_percent(uint8_t volume)
{
    switch (volume) {
    case 1:  return MUYU_VOLUME_PCT_1;
    case 2:  return MUYU_VOLUME_PCT_2;
    case 3:  return MUYU_VOLUME_PCT_3;
    default: return 0;
    }
}

// 自动模式的下一次敲击排期：从 now_ms 起按当前档位随机取一个间隔。
static void schedule_auto(muyu_state_t *s, int64_t now_ms)
{
    s->next_auto_ms = now_ms + (int64_t)muyu_auto_interval_ms(s->auto_level, &s->rng);
}

void muyu_init(muyu_state_t *s, uint32_t total, uint8_t volume, uint32_t seed, int64_t now_ms)
{
    if (s == NULL) return;
    *s = (muyu_state_t){
        .total = total,
        .session = 0,
        .volume = volume < MUYU_LEVELS ? volume : MUYU_VOLUME_DEFAULT,
        .auto_level = 0,
        .screen = MUYU_SCREEN_ON,
        .last_input_ms = now_ms,
        .last_strike_ms = now_ms,
        .next_auto_ms = 0,
        .auto_since_ms = 0,
        // 以开机时刻作为“上次保存”，开机后第一次写入至少间隔 MUYU_SAVE_MIN_GAP_MS。
        .last_save_ms = now_ms,
        .dirty = false,
        .sleep_requested = false,
        .rng = seed != 0 ? seed : 0x9E3779B9u,
    };
}

uint32_t muyu_handle_key(muyu_state_t *s, muyu_key_t key, int64_t now_ms)
{
    if (s == NULL || key == MUYU_KEY_NONE) return 0;

    // 任何有效按键都算“有人在用”：重置空闲计时并撤销休眠请求。
    s->last_input_ms = now_ms;
    s->sleep_requested = false;

    // 熄屏时按键只负责点亮屏幕。用户看不到界面，此时触发计数或切换设置都属于误操作。
    if (s->screen == MUYU_SCREEN_OFF) {
        s->screen = MUYU_SCREEN_ON;
        return MUYU_ACT_BACKLIGHT;
    }

    uint32_t act = 0;
    // 调暗时界面仍可见，按键照常生效，同时恢复亮度。
    if (s->screen == MUYU_SCREEN_DIM) {
        s->screen = MUYU_SCREEN_ON;
        act |= MUYU_ACT_BACKLIGHT;
    }

    switch (key) {
    case MUYU_KEY_STRIKE:
        act |= do_strike(s, now_ms);
        break;
    case MUYU_KEY_AUTO: {
        const bool was_on = s->auto_level != 0;
        s->auto_level = (uint8_t)((s->auto_level + 1u) % MUYU_LEVELS);
        if (s->auto_level != 0) {
            // 每次换档都重新开始 10 分钟限时：用户刚操作过，说明仍在使用。
            s->auto_since_ms = now_ms;
            // 从“不自动”进入一档时立即敲第一下，给出即时反馈；档位之间切换只按新节奏
            // 重新排期，避免每按一次上键都多敲一下。
            if (!was_on) act |= do_strike(s, now_ms);
            schedule_auto(s, now_ms);
        }
        act |= MUYU_ACT_REFRESH;
        break;
    }
    case MUYU_KEY_VOLUME:
        s->volume = (uint8_t)((s->volume + 1u) % MUYU_LEVELS);
        s->dirty = true;  // 音量档也持久化
        act |= MUYU_ACT_REFRESH;
        break;
    default:
        break;
    }
    return act;
}

uint32_t muyu_tick(muyu_state_t *s, int64_t now_ms)
{
    if (s == NULL) return 0;
    uint32_t act = 0;

    // 1) 自动敲击：先判断限时，再判断是否到点。
    if (s->auto_level != 0) {
        if (now_ms - s->auto_since_ms >= MUYU_AUTO_MAX_MS) {
            s->auto_level = 0;
            act |= MUYU_ACT_REFRESH;
        } else if (now_ms >= s->next_auto_ms) {
            act |= do_strike(s, now_ms);
            // 从本次实际敲击时刻重新排期：tick 被长时间延误（例如 LVGL 锁等待）时
            // 不补敲，避免恢复后连续爆发多下敲击。
            schedule_auto(s, now_ms);
        }
    }

    // 2) 背光：只在 tick 中“变暗”，变亮只由按键触发。
    const int64_t idle = now_ms - s->last_input_ms;
    muyu_screen_t target = MUYU_SCREEN_ON;
    if (idle >= MUYU_OFF_AFTER_MS) target = MUYU_SCREEN_OFF;
    else if (idle >= MUYU_DIM_AFTER_MS) target = MUYU_SCREEN_DIM;
    if (target > s->screen) {
        s->screen = target;
        act |= MUYU_ACT_BACKLIGHT;
    }

    // 3) 保存：最后一次敲击后静默一会儿（音效已播完），且距上次写入足够久。
    if (s->dirty &&
        now_ms - s->last_strike_ms >= MUYU_SAVE_QUIET_MS &&
        now_ms - s->last_save_ms >= MUYU_SAVE_MIN_GAP_MS) {
        act |= MUYU_ACT_SAVE;
    }

    // 4) 休眠：自动敲击期间不休眠（用户明确让它一直敲）；只请求一次。
    if (s->auto_level == 0 && !s->sleep_requested && idle >= MUYU_SLEEP_AFTER_MS) {
        s->sleep_requested = true;
        act |= MUYU_ACT_SLEEP;
    }
    return act;
}

void muyu_mark_saved(muyu_state_t *s, bool ok, int64_t now_ms)
{
    if (s == NULL) return;
    // 无论成败都刷新时间戳：失败时也按最小间隔限速重试，避免每个 tick 都去写 Flash。
    s->last_save_ms = now_ms;
    if (ok) s->dirty = false;
}

void muyu_sleep_deferred(muyu_state_t *s, int64_t now_ms, int64_t retry_ms)
{
    if (s == NULL) return;
    // 把“最近一次操作”回拨到恰好在 retry_ms 后满足休眠条件；空闲时间仍远超熄屏阈值，
    // 所以不会让屏幕重新亮起或改变背光状态。
    s->last_input_ms = now_ms - MUYU_SLEEP_AFTER_MS + retry_ms;
    s->sleep_requested = false;
}

uint8_t muyu_backlight_percent(muyu_screen_t screen)
{
    switch (screen) {
    case MUYU_SCREEN_DIM: return MUYU_BACKLIGHT_DIM;
    case MUYU_SCREEN_OFF: return MUYU_BACKLIGHT_OFF;
    case MUYU_SCREEN_ON:
    default:              return MUYU_BACKLIGHT_ACTIVE;
    }
}

bool muyu_total_needs_small_font(uint32_t total)
{
    // 44 px 数字字宽约 22 px：9 位约 200 px 可放下，10 位约 224 px 会贴边折行。
    return total >= 1000000000u;
}

const char *muyu_float_text(bool milestone)
{
    // 这里的每个汉字都必须出现在 assets/fonts/muyu_font_chars.txt，并重新生成字体。
    return milestone ? "百八烦恼尽消" : "功德 +1";
}

void muyu_mallet_head(int progress, int *x, int *y)
{
    if (progress < 0) progress = 0;
    if (progress > 1000) progress = 1000;
    const float deg = (float)MUYU_MALLET_REST_DEG +
                      (float)(MUYU_MALLET_HIT_DEG - MUYU_MALLET_REST_DEG) * (float)progress / 1000.0f;
    const float rad = deg * MUYU_PI / 180.0f;
    if (x) *x = MUYU_MALLET_PIVOT_X + (int)lroundf((float)MUYU_MALLET_LEN * cosf(rad));
    if (y) *y = MUYU_MALLET_PIVOT_Y + (int)lroundf((float)MUYU_MALLET_LEN * sinf(rad));
}
