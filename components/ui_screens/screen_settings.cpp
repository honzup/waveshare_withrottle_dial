#include "screen_mgr.h"
#include "withrottle_client.h"
#include "wifi.h"
#include "netprov.h"
#include "ui.h"
#include "lvgl.h"
#include <string>

static void go_home(lv_event_t*) { screen_mgr_show(Screen::HOME); }

static bool s_wired = false;

static void enter() {
    if (!s_wired) {
        // Back button: generated cb targets Main directly; rebind via screen_mgr.
        lv_obj_remove_event_cb(ui_Settings_Back_Btn, ui_event_Settings_Back_Btn);
        lv_obj_add_event_cb(ui_Settings_Back_Btn, go_home, LV_EVENT_CLICKED, nullptr);

        // Replace the generated back-arrow image with a home icon, for
        // consistency with the other screens.
        lv_obj_set_style_bg_img_opa(ui_Settings_Back_Btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t* home_lbl = lv_label_create(ui_Settings_Back_Btn);
        lv_label_set_text(home_lbl, LV_SYMBOL_HOME);
        lv_obj_set_style_text_font(home_lbl, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(home_lbl, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(home_lbl);

        lv_obj_t* reconf = lv_btn_create(ui_Settings_Screen);
        lv_obj_set_size(reconf, 240, 56);
        lv_obj_align(reconf, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_set_style_radius(reconf, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(reconf, lv_color_hex(0x2C4A7E), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t* rl = lv_label_create(reconf);
        lv_label_set_text(rl, "Reconfigure Wi-Fi");
        lv_obj_set_style_text_color(rl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(rl);
        // netprov_start() switches the radio to APSTA; the first press may block
        // the UI task ~10ms during the WiFi mode transition. Acceptable for a button.
        lv_obj_add_event_cb(reconf, [](lv_event_t*){
            netprov_start();
            screen_mgr_show(Screen::PROVISIONING);
        }, LV_EVENT_CLICKED, nullptr);

        s_wired = true;
    }
    lv_scr_load(ui_Settings_Screen);
}

static void tick() {
    if (ui_Wifi_Value) lv_label_set_text(ui_Wifi_Value, get_current_ssid().c_str());
    if (ui_IP_Value)   lv_label_set_text(ui_IP_Value,   get_current_ip_address().c_str());
    if (ui_withrottle_url_value) {
        std::string url = withr::get_server_url();
        if (!url.empty()) lv_label_set_text(ui_withrottle_url_value, url.c_str());
    }
}

ScreenModule g_screen_settings{ enter, nullptr, tick, nullptr };
