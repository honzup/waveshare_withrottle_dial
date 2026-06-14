#include "screen_mgr.h"
#include "withrottle_client.h"
#include "jmri_discovery.h"
#include "lvgl.h"
#include "netprov.h"
#include "wifi.h"
#include "esp_timer.h"

static lv_obj_t* s_scr     = nullptr;
static lv_obj_t* s_label   = nullptr;
static uint64_t  s_enter_us = 0;  // set in enter(); 0 until first enter (enter() always precedes tick())

static const char* phase_text(withr::Phase p) {
    switch (p) {
        case withr::Phase::WIFI:   return "Connecting Wi-Fi...";
        case withr::Phase::SERVER: return "Connecting to JMRI...";
        case withr::Phase::ROSTER: return "Loading roster...";
        case withr::Phase::READY:  return "Ready";
    }
    return "";
}

static void enter() {
    if (!s_scr) {
        s_scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t* spin = lv_spinner_create(s_scr, 1000, 60);
        lv_obj_set_size(spin, 100, 100);
        lv_obj_align(spin, LV_ALIGN_CENTER, 0, -30);
        s_label = lv_label_create(s_scr);
        lv_obj_set_style_text_font(s_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_label, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 70);
    }
    lv_scr_load(s_scr);
    s_enter_us = esp_timer_get_time();
}

static void tick() {
    withr::Phase p = withr::get_phase();
    if (s_label) lv_label_set_text(s_label, phase_text(p));

    // Enter provisioning if there are no stored creds, or Wi-Fi has failed to
    // associate for 30s. (Once associated, p advances past WIFI and we skip this.)
    if (!netprov_is_active() && p == withr::Phase::WIFI) {
        bool no_creds  = !wifi_has_credentials(); // true only when creds are blank; NVS-cleared devices still have compile-time SECRET_* defaults, so they auto-provision only via the 30s timeout below
        bool timed_out = (esp_timer_get_time() - s_enter_us) > 30ULL * 1000 * 1000;
        if (no_creds || timed_out) {
            netprov_start();
            screen_mgr_show(Screen::PROVISIONING);
            return;
        }
    }

    if (p == withr::Phase::SERVER && !withr::get_server_choices().empty()) {
        screen_mgr_show(Screen::PROVISIONING);
        return;
    }
    if (p == withr::Phase::READY) {
        // On the first connect go to the menu; after a reconnect where a loco
        // was active, drop straight back onto the throttle.
        screen_mgr_show(withr::want_loco() ? Screen::THROTTLE : Screen::HOME);
    }
}

ScreenModule g_screen_connecting{ enter, nullptr, tick, nullptr };
