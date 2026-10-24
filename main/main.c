// main/main.c —— “敲木鱼，攒功德”应用入口：初始化外设、启动应用任务、分发按键。
//
// 交互（详见 main/muyu_core.h）：
//   确定 按下   敲一下木鱼，功德 +1（用按下瞬间触发，手感跟手）
//   上键 单击   自动敲击档位循环：不自动 → 一档(0.2–0.4 s) → 二档(0.5–0.8 s)
//               → 三档(1–1.5 s) → 不自动；间隔在档内随机，连续自动最长 10 分钟
//   下键 单击   音量档位循环：静音 → 一档 → 二档 → 三档 → 静音
// （三键共用一个 ADC 分压脚，上键按下即接地，“上+下同时按”与单按上键电压相同，
//   硬件上无法识别组合键，因此不设计组合键操作。）
//   屏幕熄灭时  任意键只点亮屏幕
// 无操作 20 s 调暗、45 s 熄屏、3 min（非自动模式）进入 deep sleep；任意键唤醒并重启，
// 累计功德与音量档保存在 NVS 中，重启后恢复。
//
// 任务模型：
//   按键回调（esp_timer 任务）  只把事件放进队列，立即返回；
//   应用任务 muyu_app            唯一持有 muyu_state_t，串行处理按键与定时 tick，
//                                 短暂持 LVGL 锁刷新 UI，并负责 NVS 写入与休眠流程；
//   音频任务 muyu_audio          阻塞式 I2S 播放敲击音；
//   LVGL 任务（BSP 创建）        渲染与执行动画。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "muyu_audio.h"
#include "muyu_core.h"
#include "muyu_fonts.h"
#include "muyu_store.h"
#include "muyu_ui.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "muyu";

#define INPUT_QUEUE_DEPTH   8      // 按键事件队列深度：连敲时短暂堆积也不丢
#define APP_TASK_STACK      4096   // 应用任务栈：含 NVS 写入与 LVGL 调用
#define APP_TASK_PRIO       5
#define APP_TICK_MS         40     // 无按键时的最长等待：驱动自动敲击与背光状态机
#define BATTERY_PERIOD_MS   30000  // 电量刷新周期：电量变化慢，I2C 读取不必频繁
#define UI_LOCK_MS          200    // 刷新 UI 时等待 LVGL 锁的上限
#define SLEEP_RETRY_MS      5000   // 入睡时按键被按住，隔多久再试

// 按键事件：回调与应用任务之间传递。
typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;     // 按键事件队列（回调 → 应用任务）
static volatile bool s_input_ready;     // 应用任务就绪后才接收按键
static muyu_state_t s_state;            // 应用状态：只由应用任务访问
static bool s_audio_ok;                 // 音频是否可用：启动后只读
static volatile bool s_battery_ok;      // 电量计初始化成功后置位，之后才允许读取

// 深睡按键唤醒时，那颗键在整个启动期间仍被按着：button 组件启动后会把它判成
// 一次按下/长按，若不拦截就会凭空敲一下或切换设置。守卫在那颗键抬起前丢弃事件；
// 截止时间只是兜底，防止卡键让设备一直无响应。状态机在 key_guard.h，有 host 测试。
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// 把 BSP 按键事件翻译成应用语义。只有这里知道物理按键与功能的对应关系。
static muyu_key_t translate_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn == BSP_BTN_OK && ev == BSP_BTN_PRESS) return MUYU_KEY_STRIKE;
    if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) return MUYU_KEY_AUTO;
    if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) return MUYU_KEY_VOLUME;
    return MUYU_KEY_NONE;
}

// 终端 deep sleep：保存数据，按 CW2017 → ES8311 → I2S → I2C → LCD 的顺序关闭外设，
// 以三键共用的 GPIO0 低电平作为唤醒源。唤醒即重启。只在应用任务中调用。
// 返回说明没能入睡（按键被按住或唤醒源武装失败），设备保持正常运行。
static void enter_deep_sleep(void)
{
    // 保存放在最前面：后面任何一步失败都可能以重启收场。
    if (s_state.dirty) {
        esp_err_t e = muyu_store_save(s_state.total, s_state.volume);
        muyu_mark_saved(&s_state, e == ESP_OK, now_ms());
        if (e != ESP_OK) ESP_LOGE(TAG, "休眠前保存失败: %s", esp_err_to_name(e));
    }

    // 按键被按住时低电平唤醒条件在入睡瞬间就成立，会立刻醒来：此时放弃这次休眠。
    const int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) {
        ESP_LOGW(TAG, "按键被按住（%d mV），%d s 后重试休眠", mv, SLEEP_RETRY_MS / 1000);
        muyu_sleep_deferred(&s_state, now_ms(), SLEEP_RETRY_MS);
        return;
    }

    ESP_LOGI(TAG, "长时间无操作，进入 deep sleep（任意键唤醒），累计功德 %lu",
             (unsigned long)s_state.total);
    s_input_ready = false;

    // 先停音频任务，之后才能安全地让 codec 休眠。
    if (muyu_audio_stop() != ESP_OK) {
        // 任务可能卡在 I2S 写入中，继续关外设有风险：直接重启恢复。
        esp_restart();
    }

    // 从这里开始是终端流程：按键交回数字输入后本次运行无法恢复，失败只能重启。
    int level = 0;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK) {
        ESP_LOGE(TAG, "按键交回失败，重启");
        esp_restart();
    }
    if (level != 1) ESP_LOGW(TAG, "入睡前按键仍被按住，可能立即唤醒");
    // 第一个参数是位掩码，不是引脚号。任一键都把 GPIO0 拉低。
    esp_err_t e = esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (e != ESP_OK) {
        // 没有唤醒源就睡下去会变“砖”（只能靠复位），宁可重启继续运行。
        ESP_LOGE(TAG, "GPIO 唤醒武装失败: %s，重启", esp_err_to_name(e));
        esp_restart();
    }

    // CW2017 与 ES8311 共用 I2C：先完成两者的写入/回读，再释放总线引脚。
    if ((e = bsp_battery_sleep()) != ESP_OK) ESP_LOGW(TAG, "CW2017 休眠失败: %s", esp_err_to_name(e));
    if ((e = bsp_audio_sleep()) != ESP_OK) ESP_LOGW(TAG, "ES8311 休眠失败: %s", esp_err_to_name(e));
    if ((e = bsp_audio_prepare_deep_sleep()) != ESP_OK) ESP_LOGW(TAG, "I2S 引脚释放失败: %s", esp_err_to_name(e));
    if ((e = bsp_i2c_prepare_deep_sleep()) != ESP_OK) ESP_LOGW(TAG, "I2C 引脚释放失败: %s", esp_err_to_name(e));

    // 持 LVGL 锁等待当前 flush 完成，并阻止 LCD 关闭后再刷屏；之后不再解锁。
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "无法停止 LVGL 刷屏，重启");
        esp_restart();
    }
    if ((e = bsp_display_prepare_deep_sleep()) != ESP_OK) ESP_LOGW(TAG, "LCD 休眠失败: %s", esp_err_to_name(e));

    esp_deep_sleep_start();
    // 不应返回；外设已不可恢复，重启。
    esp_restart();
}

// 执行状态机返回的动作位图。只在应用任务中调用。
static void apply_actions(uint32_t act)
{
    if (act == 0) return;

    if ((act & MUYU_ACT_STRIKE) && s_state.volume != 0 && s_audio_ok) {
        // 先发声音再画动画：音频任务优先级更高，声音与画面基本同步。
        muyu_audio_strike();
    }
    if ((act & MUYU_ACT_REFRESH) && s_audio_ok) {
        // 音量档可能变了：交给音频任务在播放间隙写入 codec。静音档不播放，无需改 codec。
        if (s_state.volume != 0) muyu_audio_set_volume(muyu_volume_percent(s_state.volume));
    }
    if (act & MUYU_ACT_BACKLIGHT) {
        bsp_display_backlight(muyu_backlight_percent(s_state.screen));
    }
    if (act & (MUYU_ACT_STRIKE | MUYU_ACT_REFRESH)) {
        if (bsp_lvgl_lock(UI_LOCK_MS)) {
            if (act & MUYU_ACT_STRIKE) {
                muyu_ui_set_counts(s_state.total, s_state.session);
                const bool milestone = (act & MUYU_ACT_MILESTONE) != 0;
                muyu_ui_strike(muyu_float_text(milestone), milestone);
            }
            if (act & MUYU_ACT_REFRESH) {
                muyu_ui_set_status(s_state.volume, s_audio_ok, s_state.auto_level);
            }
            bsp_lvgl_unlock();
        }
    }
    if (act & MUYU_ACT_SAVE) {
        // Flash 写入可能短暂关闭 cache；状态机已保证此时没有音效在播放。
        esp_err_t e = muyu_store_save(s_state.total, s_state.volume);
        muyu_mark_saved(&s_state, e == ESP_OK, now_ms());
        if (e != ESP_OK) ESP_LOGW(TAG, "保存失败，稍后重试: %s", esp_err_to_name(e));
    }
    if (act & MUYU_ACT_SLEEP) {
        enter_deep_sleep();
    }
}

// 读电量并刷新右上角。在应用任务中周期调用，启动时由 main 任务调用一次
// （I2C 读取会阻塞几毫秒；LVGL 访问加锁）。
static void refresh_battery(void)
{
    if (!s_battery_ok) return;   // 初始化未完成或失败：保持 "--"，也不与初始化并发访问芯片
    const int soc = bsp_battery_soc();
    if (bsp_lvgl_lock(UI_LOCK_MS)) {
        muyu_ui_set_battery(soc);
        bsp_lvgl_unlock();
    }
}

static void app_task(void *arg)
{
    (void)arg;
    int64_t next_battery = now_ms() + BATTERY_PERIOD_MS;
    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, pdMS_TO_TICKS(APP_TICK_MS)) == pdTRUE) {
            const muyu_key_t key = translate_key(input.btn, input.event);
            apply_actions(muyu_handle_key(&s_state, key, now_ms()));
        }
        const int64_t t = now_ms();
        apply_actions(muyu_tick(&s_state, t));
        if (t >= next_battery) {
            next_battery = t + BATTERY_PERIOD_MS;
            refresh_battery();
        }
    }
}

// 按键回调运行在共享 esp_timer 任务里：只入队，立刻返回。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    // 守卫必须先于就绪检查：松手事件可能落在“启动后电压采样”与“应用就绪”之间，
    // 若在这里被丢弃，守卫会一直武装到兜底超时，吞掉下一次真实按键。
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "唤醒键已松手，按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "敲木鱼，攒功德 启动");
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();
    if (wake_causes & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
        ESP_LOGI(TAG, "按键唤醒：先屏蔽按键动作，直到那颗键松手");
    }

    bsp_i2c_init();

    // 屏幕是唯一的交互载体，失败就无法继续。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }

    // 读出累计功德。NVS 不可用时从 0 开始并继续运行（保存会失败并记录日志）。
    uint32_t total = 0;
    uint8_t volume = MUYU_VOLUME_DEFAULT;
    if (muyu_store_init() == ESP_OK) {
        esp_err_t e = muyu_store_load(&total, &volume, MUYU_VOLUME_DEFAULT);
        if (e != ESP_OK) ESP_LOGW(TAG, "读取功德失败: %s", esp_err_to_name(e));
    }
    // 自动敲击的随机节奏用硬件随机数做种子，每次开机节奏不同。
    muyu_init(&s_state, total, volume, esp_random(), now_ms());
    ESP_LOGI(TAG, "累计功德 %lu，音量档 %u", (unsigned long)total, (unsigned)s_state.volume);

    // 音频失败不阻塞：降级为无声，左上角显示红色静音。
    s_audio_ok = muyu_audio_start(muyu_volume_percent(s_state.volume)) == ESP_OK;

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "获取 LVGL 锁失败");
        return;
    }
    muyu_fonts_init();
    const bool ui_ok = muyu_ui_create();
    if (ui_ok) {
        muyu_ui_set_counts(s_state.total, s_state.session);
        muyu_ui_set_status(s_state.volume, s_audio_ok, s_state.auto_level);
        muyu_ui_set_battery(-1);   // 电量计初始化约需 2 s，先显示占位
    }
    bsp_lvgl_unlock();
    if (!ui_ok) {
        ESP_LOGE(TAG, "界面创建失败（LVGL 内存不足？）");
        return;
    }
    // 先画好第一帧再点亮背光，避免开机闪一下未初始化的画面。
    bsp_display_backlight(MUYU_BACKLIGHT_ACTIVE);

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (s_input_queue == NULL ||
        xTaskCreate(app_task, "muyu_app", APP_TASK_STACK, NULL, APP_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "应用任务创建失败");
        return;
    }

    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败，只能显示不能操作");
        return;
    }
    if (s_key_guard.armed) {
        // 唤醒键若在按键驱动启动前就已松手，不会再有 release 事件：按真实电压同步一次。
        const bool released = bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN;
        key_guard_sync_released(&s_key_guard, released);
        if (!s_key_guard.armed) ESP_LOGI(TAG, "唤醒键在按键驱动启动前已松手，提前撤防");
    }
    s_input_ready = true;

    // 电量计初始化可能要等数秒才取得有效 SOC（见 bsp_battery.c）。放在界面亮起、按键
    // 可用之后，在本（main）任务里完成，开机既不黑屏也不卡按键。失败时保持 "--"。
    // I2C 总线驱动自带总线锁，与音频任务的 codec 访问并发是安全的。
    s_battery_ok = bsp_battery_init() == ESP_OK;
    if (!s_battery_ok) ESP_LOGW(TAG, "电量计不可用");
    else refresh_battery();
    ESP_LOGI(TAG, "就绪：音频=%d 电量计=%d", s_audio_ok, s_battery_ok);
}
