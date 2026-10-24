// main/muyu_store.h —— 功德与设置的 NVS 持久化。
//
// 只存两个值：累计功德（u32）和音量档（u8，0..3），位于命名空间 "muyu"。
// 写入时机由 muyu_core 的保存策略决定（停手后、限速），本模块只负责读写。
// 所有函数都会阻塞（Flash 访问），只能在应用任务中调用，不得在按键回调或 LVGL 锁内调用。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// 初始化 NVS 分区。分区版本不兼容或无空闲页时按 ESP-IDF 惯例擦除后重建
// （这会丢失旧数据，只在分区不可用时发生）。
esp_err_t muyu_store_init(void);

// 读取已保存的值。键不存在（首次开机）时填默认值 0 / default_volume 并返回 ESP_OK；
// 旧版本只存了声音开关（"sound"）时按 关 → 0 档、开 → default_volume 迁移。
// 其他错误返回错误码，输出仍为默认值。读出的音量档不做范围检查，由调用方处理。
esp_err_t muyu_store_load(uint32_t *total, uint8_t *volume, uint8_t default_volume);

// 写入并提交。返回 ESP_OK 才表示已落盘。
esp_err_t muyu_store_save(uint32_t total, uint8_t volume);
