#include "screen_mgr.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include <stdio.h>

// App name + version shown on the startup splash. The version follows git tags:
// ESP-IDF sets esp_app_desc.version from `git describe --tags --always --dirty`,
// so at/after a v* tag it reads e.g. "v1.1" (or "v1.1-3-gabcd-dirty" when ahead).
// We show the bare tag on the version line and the full describe on the build
// line. APP_VERSION is only the fallback for an untagged build (shows a bare
// commit hash otherwise) — keep it in step with the latest tag.
#define APP_NAME       "WiThrottle Dial"
#define APP_VERSION    "v1.2"
#define SPLASH_HOLD_US (2500000)  // 2.5 s

static lv_obj_t* s_scr      = nullptr;
static int64_t   s_enter_us = 0;

// Resolve the display version from the git-tag-derived app version. Returns the
// leading tag token (up to the first '-', e.g. "v1.1"); falls back to
// APP_VERSION when the build isn't from a v* tag (describe gives a bare hash).
static const char* resolve_version() {
    static char ver[24];
    const esp_app_desc_t* d = esp_app_get_description();
    if (d && d->version[0] == 'v') {
        size_t i = 0;
        for (; i < sizeof(ver) - 1 && d->version[i] && d->version[i] != '-'; ++i)
            ver[i] = d->version[i];
        ver[i] = '\0';
        return ver;
    }
    return APP_VERSION;
}

static void enter() {
    if (!s_scr) {
        s_scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t* name = lv_label_create(s_scr);
        lv_label_set_text(name, APP_NAME);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(name, LV_ALIGN_CENTER, 0, -24);

        lv_obj_t* ver = lv_label_create(s_scr);
        lv_label_set_text(ver, resolve_version());
        lv_obj_set_style_text_font(ver, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(ver, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(ver, LV_ALIGN_CENTER, 0, 14);

        const esp_app_desc_t* desc = esp_app_get_description();
        char buf[48];
        snprintf(buf, sizeof(buf), "build %s", desc ? desc->version : "?");
        lv_obj_t* build = lv_label_create(s_scr);
        lv_label_set_text(build, buf);
        lv_obj_set_style_text_font(build, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(build, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(build, LV_ALIGN_CENTER, 0, 54);
    }
    s_enter_us = esp_timer_get_time();
    lv_scr_load(s_scr);
}

static void tick() {
    if (esp_timer_get_time() - s_enter_us >= SPLASH_HOLD_US) {
        screen_mgr_show(Screen::CONNECTING);
    }
}

ScreenModule g_screen_splash{ enter, nullptr, tick, nullptr };
