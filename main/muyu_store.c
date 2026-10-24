// main/muyu_store.c —— 功德与设置的 NVS 持久化。说明见 muyu_store.h。
#include "muyu_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "muyu_store";

#define NVS_NAMESPACE  "muyu"
#define KEY_TOTAL      "total"   // u32：累计功德
#define KEY_VOLUME     "vol"     // u8：音量档 0..3，0 = 静音
#define KEY_SOUND_V1   "sound"   // u8：第一版的声音开关（1 = 开），只读，用于迁移

esp_err_t muyu_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区不可用（%s），擦除后重建", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
    return err;
}

esp_err_t muyu_store_load(uint32_t *total, uint8_t *volume, uint8_t default_volume)
{
    *total = 0;
    *volume = default_volume;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    // 首次开机命名空间还不存在：用默认值即可。
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    uint32_t t = 0;
    uint8_t v = 0;
    esp_err_t e1 = nvs_get_u32(h, KEY_TOTAL, &t);
    esp_err_t e2 = nvs_get_u8(h, KEY_VOLUME, &v);
    if (e2 == ESP_ERR_NVS_NOT_FOUND) {
        // 没有音量档：可能是第一版写的声音开关，按开/关迁移。下次保存时写入新键。
        uint8_t old = 1;
        if (nvs_get_u8(h, KEY_SOUND_V1, &old) == ESP_OK) {
            v = old ? default_volume : 0;
            e2 = ESP_OK;
        }
    }
    nvs_close(h);

    if (e1 == ESP_OK) *total = t;
    if (e2 == ESP_OK) *volume = v;
    if (e1 != ESP_OK && e1 != ESP_ERR_NVS_NOT_FOUND) return e1;
    if (e2 != ESP_OK && e2 != ESP_ERR_NVS_NOT_FOUND) return e2;
    return ESP_OK;
}

esp_err_t muyu_store_save(uint32_t total, uint8_t volume)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, KEY_TOTAL, total);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_VOLUME, volume);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
