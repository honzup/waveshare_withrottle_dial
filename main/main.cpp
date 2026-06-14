/*
 * main.cpp — firmware entry point. Brings up the console, Wi-Fi, battery sense,
 * the WiThrottle client, the display + generated UI and the screen manager, then
 * runs the periodic LVGL update loop and the encoder input loop.
 *
 * Originally seeded from Espressif's CC0-1.0 "hello_world" template; the current
 * contents are independently authored for waveshare_withrottle_dial.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#include <atomic>
#include <cstdio>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "display_init.h"
#include "ui.h"
#include "user_config.h"
#include "user_encoder_bsp.h"

#include "ConsoleCommands.h"
#include "Param.h"
#include "ParamMgr.h"
#include "battery_bsp.h"
#include "ota_server.h"
#include "screen_mgr.h"
#include "udp_log.h"
#include "wifi.h"
#include "withrottle_client.h"

namespace {

const char *TAG = "main";

// Encoder detents accumulate here (±50 per step) between UI ticks; written by
// the encoder task, drained by the UI task.
std::atomic<int> s_scroll{0};

// Milliseconds since boot.
inline uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Periodic UI pump: take the LVGL lock, let the active screen update and consume
// any encoder rotation, then release. Logs slow ticks and recovers from a lock
// that stays unavailable.
void ui_update_task(void *)
{
    uint32_t lock_failures = 0;
    uint32_t lock_count    = 0;

    for (;;) {
        const uint32_t attempt_start = now_ms();

        if (ui_lvgl_lock(100)) {
            ++lock_count;
            lock_failures = 0;

            const uint32_t held_start = now_ms();

            screen_mgr_tick();

            const int rotation = s_scroll.exchange(0);
            if (rotation != 0) {
                screen_mgr_on_rotate(rotation);
            }

            const uint32_t held = now_ms() - held_start;
            if (held > 100) {
                ESP_LOGW(TAG, "ui tick held the lock %lu ms (#%lu)", held, lock_count);
            }

            ui_lvgl_unlock();
        } else {
            ++lock_failures;
            ESP_LOGW(TAG, "ui tick could not take LVGL lock (#%lu, waited %lu ms)", lock_failures,
                     now_ms() - attempt_start);

            const UBaseType_t headroom = uxTaskGetStackHighWaterMark(nullptr);
            if (headroom < 512) {
                ESP_LOGW(TAG, "ui task stack low: %lu bytes free", headroom);
            }

            if (lock_failures >= 10) {
                ESP_LOGE(TAG, "ui task: %lu lock failures in a row; backing off", lock_failures);
                vTaskDelay(pdMS_TO_TICKS(500));
                lock_failures = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Encoder task: each detent event nudges the scroll accumulator one way or the
// other for the UI task to pick up.
void encoder_task(void *)
{
    for (;;) {
        const EventBits_t bits =
            xEventGroupWaitBits(knob_even_, BIT_EVEN_ALL, pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));
        if (READ_BIT(bits, 0)) s_scroll += 50;
        if (READ_BIT(bits, 1)) s_scroll -= 50;
    }
}

// Optional watchdog: if the LVGL lock is unavailable for ~10 s straight, assume
// a deadlock and reboot. Not started by default (see app_main).
void deadlock_monitor_task(void *)
{
    ESP_LOGI(TAG, "deadlock monitor running");
    uint32_t stuck = 0;

    for (;;) {
        if (ui_lvgl_lock(50)) {
            stuck = 0;
            ui_lvgl_unlock();
        } else if (++stuck >= 20) {
            ESP_LOGE(TAG, "LVGL lock stuck > 10 s — rebooting in 3 s");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        } else {
            ESP_LOGW(TAG, "deadlock monitor: lock unavailable (%lu)", stuck);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Build the generated UI while holding the LVGL lock. display_init() already
// started the render task, and that task refreshes under the lock; building the
// scene unlocked races it and can dereference a half-initialised style. If the
// lock can't be taken, build anyway rather than hang.
void build_ui_locked()
{
    if (ui_lvgl_lock(2000)) {
        ui_init();
        ui_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "no LVGL lock for ui_init; building unlocked");
        ui_init();
    }
}

}  // namespace

extern "C" void app_main(void)
{
    ConsoleCommandsInit();
    espwifi_Init();
    battery_bsp_init();
    ESP_LOGI(TAG, "WiThrottle Knob starting");

    // Start the WiThrottle client before the display/UI tasks claim internal
    // DRAM. Its FreeRTOS stack must come from internal RAM, and once LVGL, the
    // 8 KB UI task and the 32 KB protocol buffer are up that pool is spent —
    // creating it later failed. It only needs Wi-Fi, which is already up.
    withr::client_start();

    display_init();
    build_ui_locked();

    // The UI generator seeds the train list with placeholder rows; scroll the
    // container back to the top after init.
    if (ui_lvgl_lock(1000)) {
        lv_obj_scroll_by(ui_Train_Select_Container, 0, -100, LV_ANIM_OFF);
        ui_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "no LVGL lock for post-init fixup");
    }

    user_encoder_init();

    if (ui_lvgl_lock(1000)) {
        screen_mgr_init();  // shows the connecting screen
        ui_lvgl_unlock();
    }

    if (xTaskCreate(ui_update_task, "ui_update_task", 8 * 1024, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "failed to create ui_update_task");
    }
    xTaskCreate(encoder_task, "user_enc_task", 2 * 1024, nullptr, 2, nullptr);
    // xTaskCreate(deadlock_monitor_task, "deadlock_monitor", 2 * 1024, nullptr, 1, nullptr);
    (void) deadlock_monitor_task;  // kept available; not started by default

    params::ParamMgr::getInstance().listAll();

    static params::Param<std::string> s_udp_log_ip{"udp_log_ip", std::string("")};
    if (!s_udp_log_ip.get().empty()) {
        udp_log_start(s_udp_log_ip.get().c_str(), 4444);
    }
    ota_server_start();

    ESP_LOGI(TAG, "startup complete");

    // app_main's task must not return while we still want these globals around,
    // so park it here; all real work runs in the tasks started above.
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100000));
    }
}
