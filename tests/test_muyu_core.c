// 木鱼应用纯逻辑状态机的 host 测试：不依赖 SDK，直接验证 muyu_core.c。
#undef NDEBUG
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "muyu_core.h"

#define SEED 12345u

// 敲击计数、里程碑与饱和。
static void test_strike_and_milestone(void) {
    muyu_state_t s;
    muyu_init(&s, 41, 2, SEED, 0);
    assert(s.total == 41 && s.session == 0 && !s.dirty);

    uint32_t act = muyu_handle_key(&s, MUYU_KEY_STRIKE, 10);
    assert(act == MUYU_ACT_STRIKE);
    assert(s.total == 42 && s.session == 1 && s.dirty);

    // 本次第 108 下带里程碑，第 107、109 下不带。
    for (int i = 2; i < MUYU_MILESTONE_EVERY; i++) {
        act = muyu_handle_key(&s, MUYU_KEY_STRIKE, 10 + i);
        assert(!(act & MUYU_ACT_MILESTONE));
    }
    act = muyu_handle_key(&s, MUYU_KEY_STRIKE, 200);
    assert(s.session == MUYU_MILESTONE_EVERY);
    assert(act == (MUYU_ACT_STRIKE | MUYU_ACT_MILESTONE));
    act = muyu_handle_key(&s, MUYU_KEY_STRIKE, 201);
    assert(act == MUYU_ACT_STRIKE);

    // 累计到上限后饱和，不回绕成 0。
    muyu_init(&s, UINT32_MAX, 2, SEED, 0);
    muyu_handle_key(&s, MUYU_KEY_STRIKE, 1);
    assert(s.total == UINT32_MAX);

    // 无效按键不产生动作，也不刷新空闲计时。
    muyu_init(&s, 0, 2, SEED, 0);
    assert(muyu_handle_key(&s, MUYU_KEY_NONE, 5000) == 0);
    assert(s.last_input_ms == 0);
}

// 音量档：静音 → 一 → 二 → 三 → 静音循环，持久化；初始化时越界档位回到默认。
static void test_volume_levels(void) {
    muyu_state_t s;
    muyu_init(&s, 0, 0, SEED, 0);
    assert(s.volume == 0);
    const uint8_t expect[] = { 1, 2, 3, 0, 1 };
    for (int i = 0; i < 5; i++) {
        uint32_t act = muyu_handle_key(&s, MUYU_KEY_VOLUME, 100 + i);
        assert(act == MUYU_ACT_REFRESH);
        assert(s.volume == expect[i] && s.dirty && s.total == 0);
    }
    muyu_init(&s, 0, 9, SEED, 0);
    assert(s.volume == MUYU_VOLUME_DEFAULT);

    // 音量百分比：静音为 0，三档递增，最高档对应 codec 0 dB（93%），档间相差约 6 dB（12%）。
    assert(muyu_volume_percent(0) == 0);
    assert(muyu_volume_percent(4) == 0);
    assert(muyu_volume_percent(1) < muyu_volume_percent(2));
    assert(muyu_volume_percent(2) < muyu_volume_percent(3));
    assert(muyu_volume_percent(3) == 93);
    assert(muyu_volume_percent(3) - muyu_volume_percent(2) == 12);
    assert(muyu_volume_percent(2) - muyu_volume_percent(1) == 12);
}

// 自动间隔：各档随机值始终落在闭区间内，并且确实有变化（不是固定值）。
static void test_auto_interval_ranges(void) {
    static const uint32_t lo[] = { 0, MUYU_AUTO1_MIN_MS, MUYU_AUTO2_MIN_MS, MUYU_AUTO3_MIN_MS };
    static const uint32_t hi[] = { 0, MUYU_AUTO1_MAX_MS, MUYU_AUTO2_MAX_MS, MUYU_AUTO3_MAX_MS };
    assert(lo[1] == 200 && hi[1] == 400 && lo[2] == 500 && hi[2] == 800 &&
           lo[3] == 1000 && hi[3] == 1500);
    uint32_t rng = SEED;
    for (uint8_t level = 1; level <= 3; level++) {
        uint32_t min = UINT32_MAX, max = 0;
        for (int i = 0; i < 2000; i++) {
            uint32_t v = muyu_auto_interval_ms(level, &rng);
            assert(v >= lo[level] && v <= hi[level]);
            if (v < min) min = v;
            if (v > max) max = v;
        }
        // 2000 次采样应基本铺满整个区间。
        assert(min <= lo[level] + 10 && max >= hi[level] - 10);
    }
    assert(muyu_auto_interval_ms(0, &rng) == 0);
    assert(muyu_auto_interval_ms(4, &rng) == 0);
    assert(muyu_auto_interval_ms(1, NULL) == 0);

    // 种子为 0 时被替换为非 0，随机数不会卡死在 0。
    muyu_state_t s;
    muyu_init(&s, 0, 2, 0, 0);
    assert(s.rng != 0);
}

// 自动档：循环切换、进入一档立即敲、换档不额外敲、间隔落在档内、不补敲、限时停止。
static void test_auto_levels(void) {
    muyu_state_t s;
    muyu_init(&s, 0, 2, SEED, 0);

    // 不自动 → 一档：立即敲一下。
    uint32_t act = muyu_handle_key(&s, MUYU_KEY_AUTO, 1000);
    assert(s.auto_level == 1 && act == (MUYU_ACT_STRIKE | MUYU_ACT_REFRESH) && s.total == 1);

    // 一档间隔 200–400 ms：逐毫秒推进，检查每两次自动敲击的间隔。
    int64_t last = 1000;
    int strikes = 0;
    for (int64_t t = 1001; t <= 6000; t++) {
        if (muyu_tick(&s, t) & MUYU_ACT_STRIKE) {
            const int64_t gap = t - last;
            assert(gap >= MUYU_AUTO1_MIN_MS && gap <= MUYU_AUTO1_MAX_MS);
            last = t;
            strikes++;
        }
    }
    assert(strikes >= 5000 / MUYU_AUTO1_MAX_MS && strikes <= 5000 / MUYU_AUTO1_MIN_MS);

    // 一档 → 二档：不额外敲，只按新节奏排期。
    const uint32_t before = s.total;
    act = muyu_handle_key(&s, MUYU_KEY_AUTO, 6000);
    assert(s.auto_level == 2 && act == MUYU_ACT_REFRESH && s.total == before);
    last = 6000;
    for (int64_t t = 6001; t <= 12000; t++) {
        if (muyu_tick(&s, t) & MUYU_ACT_STRIKE) {
            const int64_t gap = t - last;
            assert(gap >= MUYU_AUTO2_MIN_MS && gap <= MUYU_AUTO2_MAX_MS);
            last = t;
        }
    }

    // 二档 → 三档：间隔 1–1.5 s。
    muyu_handle_key(&s, MUYU_KEY_AUTO, 12000);
    assert(s.auto_level == 3);
    last = 12000;
    for (int64_t t = 12001; t <= 22000; t++) {
        if (muyu_tick(&s, t) & MUYU_ACT_STRIKE) {
            const int64_t gap = t - last;
            assert(gap >= MUYU_AUTO3_MIN_MS && gap <= MUYU_AUTO3_MAX_MS);
            last = t;
        }
    }

    // 三档 → 不自动：不再敲。
    act = muyu_handle_key(&s, MUYU_KEY_AUTO, 22000);
    assert(s.auto_level == 0 && act == MUYU_ACT_REFRESH);
    for (int64_t t = 22001; t <= 26000; t += 10) assert(!(muyu_tick(&s, t) & MUYU_ACT_STRIKE));

    // tick 延误 5 s：只补一下，并从当前时间重新排期。
    muyu_init(&s, 0, 2, SEED, 0);
    muyu_handle_key(&s, MUYU_KEY_AUTO, 0);
    assert(muyu_tick(&s, 5000) & MUYU_ACT_STRIKE);
    assert(!(muyu_tick(&s, 5000 + MUYU_AUTO1_MIN_MS - 1) & MUYU_ACT_STRIKE));

    // 无人值守超过上限后自动停止；换档会重新计时。
    muyu_init(&s, 0, 2, SEED, 0);
    muyu_handle_key(&s, MUYU_KEY_AUTO, 0);
    muyu_handle_key(&s, MUYU_KEY_AUTO, 60000);            // 换到二档，重新计时
    act = muyu_tick(&s, MUYU_AUTO_MAX_MS);
    assert(s.auto_level == 2);
    act = muyu_tick(&s, 60000 + MUYU_AUTO_MAX_MS);
    assert(s.auto_level == 0 && (act & MUYU_ACT_REFRESH) && !(act & MUYU_ACT_STRIKE));
}

// 背光：调暗 → 熄屏；熄屏时按键只唤醒；调暗时按键照常生效。
static void test_screen_power(void) {
    muyu_state_t s;
    muyu_init(&s, 0, 2, SEED, 0);
    assert(muyu_backlight_percent(s.screen) == MUYU_BACKLIGHT_ACTIVE);

    assert(!(muyu_tick(&s, MUYU_DIM_AFTER_MS - 1) & MUYU_ACT_BACKLIGHT));
    assert(muyu_tick(&s, MUYU_DIM_AFTER_MS) & MUYU_ACT_BACKLIGHT);
    assert(s.screen == MUYU_SCREEN_DIM);
    assert(muyu_backlight_percent(s.screen) == MUYU_BACKLIGHT_DIM);
    assert(!(muyu_tick(&s, MUYU_DIM_AFTER_MS + 10) & MUYU_ACT_BACKLIGHT));

    uint32_t act = muyu_handle_key(&s, MUYU_KEY_STRIKE, MUYU_DIM_AFTER_MS + 100);
    assert(act == (MUYU_ACT_BACKLIGHT | MUYU_ACT_STRIKE));
    assert(s.screen == MUYU_SCREEN_ON && s.total == 1);

    const int64_t t_off = MUYU_DIM_AFTER_MS + 100 + MUYU_OFF_AFTER_MS;
    assert(muyu_tick(&s, t_off) & MUYU_ACT_BACKLIGHT);
    assert(s.screen == MUYU_SCREEN_OFF);
    assert(muyu_backlight_percent(s.screen) == MUYU_BACKLIGHT_OFF);

    // 熄屏时按任意键只唤醒，不计数、不切换档位。
    act = muyu_handle_key(&s, MUYU_KEY_STRIKE, t_off + 1);
    assert(act == MUYU_ACT_BACKLIGHT && s.total == 1 && s.screen == MUYU_SCREEN_ON);
    muyu_tick(&s, t_off + 1 + MUYU_OFF_AFTER_MS);
    act = muyu_handle_key(&s, MUYU_KEY_VOLUME, t_off + 2 + MUYU_OFF_AFTER_MS);
    assert(act == MUYU_ACT_BACKLIGHT && s.volume == 2);
}

// 保存时机：需要脏数据、敲击后静默、距上次写入足够久；失败保留脏标记。
static void test_save_policy(void) {
    // 静默阈值必须盖住 320 ms 的敲击音，又要短于一档最大间隔，否则一档自动时永远不保存。
    assert(MUYU_SAVE_QUIET_MS > 320 && MUYU_SAVE_QUIET_MS < MUYU_AUTO1_MAX_MS);

    muyu_state_t s;
    muyu_init(&s, 0, 2, SEED, 0);
    assert(!(muyu_tick(&s, 60000) & MUYU_ACT_SAVE));

    muyu_init(&s, 0, 2, SEED, 0);
    muyu_handle_key(&s, MUYU_KEY_STRIKE, 1000);
    assert(!(muyu_tick(&s, 4999) & MUYU_ACT_SAVE));
    assert(muyu_tick(&s, MUYU_SAVE_MIN_GAP_MS) & MUYU_ACT_SAVE);

    muyu_mark_saved(&s, false, 5000);
    assert(s.dirty);
    assert(!(muyu_tick(&s, 9999) & MUYU_ACT_SAVE));
    assert(muyu_tick(&s, 10000) & MUYU_ACT_SAVE);
    muyu_mark_saved(&s, true, 10000);
    assert(!s.dirty);

    muyu_handle_key(&s, MUYU_KEY_STRIKE, 20000);
    muyu_handle_key(&s, MUYU_KEY_STRIKE, 20300);
    assert(!(muyu_tick(&s, 20300 + MUYU_SAVE_QUIET_MS - 1) & MUYU_ACT_SAVE));
    assert(muyu_tick(&s, 20300 + MUYU_SAVE_QUIET_MS) & MUYU_ACT_SAVE);

    // 一档连续自动敲击时，30 s 内仍能找到保存窗口，且保存时距上次敲击 ≥ 静默阈值。
    muyu_init(&s, 0, 2, SEED, 0);
    muyu_handle_key(&s, MUYU_KEY_AUTO, 0);
    int saves = 0;
    for (int64_t t = 1; t <= 30000; t++) {
        uint32_t act = muyu_tick(&s, t);
        if (act & MUYU_ACT_SAVE) {
            assert(t - s.last_strike_ms >= MUYU_SAVE_QUIET_MS);
            muyu_mark_saved(&s, true, t);
            saves++;
        }
    }
    assert(saves >= 2);
}

// 休眠：长时间无操作只请求一次；自动敲击期间不休眠；按键撤销请求；可推迟重试。
static void test_sleep_policy(void) {
    muyu_state_t s;
    muyu_init(&s, 0, 2, SEED, 0);
    assert(!(muyu_tick(&s, MUYU_SLEEP_AFTER_MS - 1) & MUYU_ACT_SLEEP));
    assert(muyu_tick(&s, MUYU_SLEEP_AFTER_MS) & MUYU_ACT_SLEEP);
    assert(!(muyu_tick(&s, MUYU_SLEEP_AFTER_MS + 100) & MUYU_ACT_SLEEP));

    muyu_handle_key(&s, MUYU_KEY_STRIKE, MUYU_SLEEP_AFTER_MS + 200);
    assert(!s.sleep_requested);
    assert(muyu_tick(&s, 2 * MUYU_SLEEP_AFTER_MS + 200) & MUYU_ACT_SLEEP);

    muyu_init(&s, 0, 2, SEED, 0);
    assert(muyu_tick(&s, MUYU_SLEEP_AFTER_MS) & MUYU_ACT_SLEEP);
    assert(s.screen == MUYU_SCREEN_OFF);
    muyu_sleep_deferred(&s, MUYU_SLEEP_AFTER_MS, 5000);
    assert(muyu_tick(&s, MUYU_SLEEP_AFTER_MS + 4999) == 0);
    assert(muyu_tick(&s, MUYU_SLEEP_AFTER_MS + 5000) == MUYU_ACT_SLEEP);

    muyu_init(&s, 0, 2, SEED, 0);
    muyu_handle_key(&s, MUYU_KEY_AUTO, 0);
    muyu_handle_key(&s, MUYU_KEY_AUTO, 1);
    muyu_handle_key(&s, MUYU_KEY_AUTO, 2);              // 三档
    for (int64_t t = 0; t < MUYU_AUTO_MAX_MS; t += 1000) {
        assert(!(muyu_tick(&s, t) & MUYU_ACT_SLEEP));
    }
}

// 飘字文案、大数字字号与木槌几何。
static void test_text_and_geometry(void) {
    assert(strcmp(muyu_float_text(false), "功德 +1") == 0);
    assert(strcmp(muyu_float_text(true), "百八烦恼尽消") == 0);
    assert(!muyu_total_needs_small_font(999999999u));
    assert(muyu_total_needs_small_font(1000000000u));
    assert(muyu_total_needs_small_font(UINT32_MAX));

    int x0, y0, x1, y1, xm, ym;
    muyu_mallet_head(0, &x0, &y0);
    muyu_mallet_head(1000, &x1, &y1);
    muyu_mallet_head(500, &xm, &ym);
    assert(y0 < y1 && x0 > x1);
    assert(ym > y0 && ym < y1 && xm < x0 && xm > x1);
    int xc, yc;
    muyu_mallet_head(-50, &xc, &yc);
    assert(xc == x0 && yc == y0);
    muyu_mallet_head(5000, &xc, &yc);
    assert(xc == x1 && yc == y1);
    for (int p = 0; p <= 1000; p += 50) {
        int x, y;
        muyu_mallet_head(p, &x, &y);
        assert(x - 14 >= 30 && x + 14 <= 240 - 8 && y - 14 >= 30 && y + 14 <= 320 - 30);
    }
    const float dx = (float)(x1 - 102) / 78.0f, dy = (float)(y1 - 228) / 52.0f;
    const float r2 = dx * dx + dy * dy;
    assert(r2 > 1.0f && r2 < 1.9f);
}

int main(void) {
    test_strike_and_milestone();
    test_volume_levels();
    test_auto_interval_ranges();
    test_auto_levels();
    test_screen_power();
    test_save_policy();
    test_sleep_policy();
    test_text_and_geometry();
    printf("test_muyu_core: PASS\n");
    return 0;
}
