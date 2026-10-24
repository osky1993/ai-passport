// main/muyu_audio.c —— 木鱼敲击音效播放任务。说明见 muyu_audio.h。
#include "muyu_audio.h"

#include "muyu_tone.h"
#include "bsp_audio.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "muyu_audio";

#define CHUNK_SAMPLES       320    // 每次写入 20 ms（16 kHz）：在两块之间检查打断/停止请求
#define AUDIO_TASK_STACK    3072   // 任务只做通知等待与 I2S 写入，3 KB 足够
#define AUDIO_TASK_PRIO     6      // 高于应用任务（5），保证敲击声及时送入 DMA
#define AUDIO_STOP_WAIT_MS  1000

// 通知位：同一任务通知值上的三个独立位。
#define NOTIFY_STRIKE  (1u << 0)
#define NOTIFY_STOP    (1u << 1)
#define NOTIFY_VOLUME  (1u << 2)   // s_volume_pct 已更新，需要写入 codec
#define NOTIFY_ALL     (NOTIFY_STRIKE | NOTIFY_STOP | NOTIFY_VOLUME)

static TaskHandle_t s_task;            // 播放任务句柄；仅由启动/停止路径写
static SemaphoreHandle_t s_stopped;    // 任务退出确认
static volatile bool s_ready;          // 任务运行中且 codec 可用
// 待生效的 codec 音量百分比：应用任务写，音频任务在收到 NOTIFY_VOLUME 后读。
// 单字节读写天然原子，配合任务通知即可，无需加锁。
static volatile uint8_t s_volume_pct;

// 在音频任务里把音量写入 codec：与 PCM 写入串行，避免两个任务同时操作 codec。
static void apply_volume(void)
{
    bsp_audio_set_volume(s_volume_pct);
}

// 播放一遍敲击音。返回 true 表示中途收到了新的敲击请求（需要从头再播），
// stop 通过 *stop_requested 返回。
static bool play_once(bool *stop_requested)
{
    size_t pos = 0;
    while (pos < muyu_knock_samples) {
        // 非阻塞检查：有新请求就打断当前这一遍。
        uint32_t bits = 0;
        if (xTaskNotifyWait(0, NOTIFY_ALL, &bits, 0) == pdTRUE) {
            if (bits & NOTIFY_VOLUME) apply_volume();
            if (bits & NOTIFY_STOP) { *stop_requested = true; return false; }
            if (bits & NOTIFY_STRIKE) return true;
        }
        size_t n = muyu_knock_samples - pos;
        if (n > CHUNK_SAMPLES) n = CHUNK_SAMPLES;
        // PCM 位于 Flash（只读常量）；I2S 驱动会把数据拷进 DMA 缓冲，可直接传入。
        if (bsp_audio_write(&muyu_knock_pcm[pos], n * sizeof(int16_t)) != ESP_OK) {
            ESP_LOGW(TAG, "I2S 写入失败，放弃本次音效");
            return false;
        }
        pos += n;
    }
    return false;
}

static void audio_task(void *arg)
{
    (void)arg;
    bool stop = false;
    while (!stop) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, NOTIFY_ALL, &bits, portMAX_DELAY);
        if (bits & NOTIFY_VOLUME) apply_volume();
        if (bits & NOTIFY_STOP) break;
        if (!(bits & NOTIFY_STRIKE)) continue;
        // 被新敲击打断时立刻从头播放；最多连续重播有限次以防异常情况下死循环。
        for (int replay = 0; replay < 64; replay++) {
            if (!play_once(&stop) || stop) break;
        }
    }
    s_ready = false;
    // 确认之后不再访问任何共享状态；任务挂起，由停止方删除。
    xSemaphoreGive(s_stopped);
    for (;;) vTaskSuspend(NULL);
}

esp_err_t muyu_audio_start(uint8_t volume_pct)
{
    if (s_task != NULL) return ESP_OK;

    esp_err_t err = bsp_audio_init();
    if (err == ESP_OK) err = bsp_audio_set_format(MUYU_TONE_RATE_HZ, 16, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "音频初始化失败，以无声模式运行: %s", esp_err_to_name(err));
        return err;
    }
    // 任务尚未创建，此处直接写 codec 不会与播放并发。
    s_volume_pct = volume_pct;
    bsp_audio_set_volume(volume_pct);

    s_stopped = xSemaphoreCreateBinary();
    if (s_stopped == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreate(audio_task, "muyu_audio", AUDIO_TASK_STACK, NULL,
                    AUDIO_TASK_PRIO, &s_task) != pdPASS) {
        vSemaphoreDelete(s_stopped);
        s_stopped = NULL;
        s_task = NULL;
        ESP_LOGE(TAG, "创建音频任务失败");
        return ESP_ERR_NO_MEM;
    }
    s_ready = true;
    return ESP_OK;
}

bool muyu_audio_ready(void)
{
    return s_ready;
}

void muyu_audio_strike(void)
{
    TaskHandle_t task = s_task;
    if (s_ready && task != NULL) xTaskNotify(task, NOTIFY_STRIKE, eSetBits);
}

void muyu_audio_set_volume(uint8_t percent)
{
    s_volume_pct = percent;
    TaskHandle_t task = s_task;
    if (s_ready && task != NULL) xTaskNotify(task, NOTIFY_VOLUME, eSetBits);
}

esp_err_t muyu_audio_stop(void)
{
    TaskHandle_t task = s_task;
    if (task == NULL) return ESP_OK;
    xTaskNotify(task, NOTIFY_STOP, eSetBits);
    if (xSemaphoreTake(s_stopped, pdMS_TO_TICKS(AUDIO_STOP_WAIT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "音频任务停止超时");
        return ESP_ERR_TIMEOUT;
    }
    vTaskDelete(task);
    s_task = NULL;
    vSemaphoreDelete(s_stopped);
    s_stopped = NULL;
    return ESP_OK;
}
