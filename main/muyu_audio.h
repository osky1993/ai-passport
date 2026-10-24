// main/muyu_audio.h —— 木鱼敲击音效的播放工作任务。
//
// bsp_audio_write() 是阻塞调用，不能放在按键回调或 LVGL 任务里，所以由独立任务
// 播放。敲击请求通过任务通知传递，调用方永不阻塞；播放中途收到新请求会从头
// 重新播放，快速连敲时每一下都有清晰的起音。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// 初始化 codec（bsp_audio_init）、设置初始音量并启动播放任务。volume_pct 为 codec
// 音量百分比（见 muyu_volume_percent()）。失败时返回错误码，应用继续以无声模式运行。
// 调用线程：应用启动阶段，单次调用。
esp_err_t muyu_audio_start(uint8_t volume_pct);

// 音频是否可用（启动成功且未停止）。
bool muyu_audio_ready(void);

// 请求播放一次敲击音。非阻塞，任意任务可调用；音频不可用时什么都不做。
void muyu_audio_strike(void);

// 修改 codec 音量百分比。非阻塞：由音频任务在播放间隙写入 codec，避免与 PCM 写入并发。
void muyu_audio_set_volume(uint8_t percent);

// 停止播放任务并等待其确认退出（最多约 1 s），用于进入 deep sleep 前。
// 返回 ESP_ERR_TIMEOUT 时任务仍可能阻塞在 I2S 写入中，调用方不得继续操作音频外设。
esp_err_t muyu_audio_stop(void);
