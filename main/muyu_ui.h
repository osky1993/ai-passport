// main/muyu_ui.h —— “敲木鱼，攒功德”的 LVGL 页面。
//
// 整页为本应用重新设计：深色禅意背景、顶部状态栏（声音/自动/电量）、
// 中部“功德”大数字、下方用 LVGL 基本图形绘制的木鱼、坐垫与木槌，
// 以及敲击时的木槌下落、木鱼下沉、金色涟漪和上浮飘字动画。不使用任何图片素材。
//
// 线程上下文：除特别说明外，所有函数都必须在持有 bsp_lvgl_lock() 时调用
// （或位于 LVGL 任务内）。动画回调由 LVGL 任务执行，天然持锁。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 创建并载入页面。需先调用 muyu_fonts_init()。重复调用无副作用。
// 返回 false 表示对象创建失败（LVGL 内存池耗尽），调用方应记录错误。
bool muyu_ui_create(void);

// 刷新累计功德与本次计数。
void muyu_ui_set_counts(uint32_t total, uint32_t session);

// 播放一次敲击动画：木槌下落回弹、木鱼下沉、涟漪扩散，并飘出 float_text。
// float_text 必须是生命周期覆盖动画的字符串（muyu_float_text() 返回的常量满足）。
// milestone 为 true 时飘字使用更醒目的颜色。连续调用会重新开始木槌动画。
void muyu_ui_strike(const char *float_text, bool milestone);

// 刷新左上角状态：音量图标与档位（0 档显示静音；音频不可用时显示红色静音），
// 以及自动敲击档位标签“自动1/2/3”（0 档隐藏）。
void muyu_ui_set_status(uint8_t volume, bool audio_ok, uint8_t auto_level);

// 刷新右上角电量。soc 取 0..100；-1 表示电量计不可用，显示 "--"。
void muyu_ui_set_battery(int soc);
