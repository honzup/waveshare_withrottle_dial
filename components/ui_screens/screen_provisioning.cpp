#include "screen_mgr.h"
#include "withrottle_client.h"
#include "jmri_discovery.h"
#include "netprov.h"
#include "lvgl.h"
#include <string>
#include <vector>

static lv_obj_t* s_scr   = nullptr;
static lv_obj_t* s_list  = nullptr;
static lv_obj_t* s_info  = nullptr;
static std::vector<jmri_discovery::JmriServer> s_shown; // currently rendered set

static void pick_clicked(lv_event_t* e) {
    intptr_t idx = (intptr_t) lv_event_get_user_data(e);
    if (idx >= 0 && idx < (intptr_t) s_shown.size()) {
        withr::set_jmri_server(s_shown[idx].ip, s_shown[idx].port);
        screen_mgr_show(Screen::CONNECTING);
    }
}

static void build_if_needed() {
    if (s_scr) return;
    s_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t* title = lv_label_create(s_scr);
    lv_label_set_text(title, "Choose JMRI server");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, 320, 250);
    lv_obj_align(s_list, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_list, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t* info = lv_label_create(s_scr);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info, 300);
    lv_label_set_text(info,
        "Join Wi-Fi 'WiThrottle-Setup' on your phone, then follow the page that opens.");
    lv_obj_set_style_text_color(info, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(info, LV_OBJ_FLAG_HIDDEN);
    s_info = info;
}

static void rebuild_rows() {
    // Reuse-or-create one button per server (never delete — matches list_widget).
    uint32_t existing = lv_obj_get_child_cnt(s_list);
    for (size_t i = 0; i < s_shown.size(); ++i) {
        lv_obj_t* btn;
        lv_obj_t* lbl;
        if (i < existing) {
            btn = lv_obj_get_child(s_list, i);
            lbl = lv_obj_get_child(btn, 0);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            btn = lv_btn_create(s_list);
            lv_obj_set_size(btn, 280, 50);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C4A7E), LV_PART_MAIN | LV_STATE_DEFAULT);
            lbl = lv_label_create(btn);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_center(lbl);
            lv_obj_add_event_cb(btn, pick_clicked, LV_EVENT_CLICKED, (void*) (intptr_t) i);
        }
        std::string name = s_shown[i].name.empty() ? s_shown[i].ip : s_shown[i].name;
        lv_label_set_text(lbl, (name + "  (" + s_shown[i].ip + ")").c_str());
    }
    for (uint32_t i = s_shown.size(); i < existing; ++i)
        lv_obj_add_flag(lv_obj_get_child(s_list, i), LV_OBJ_FLAG_HIDDEN);
}

static void tick();  // fwd decl: enter() pre-populates by invoking it

static void enter() { build_if_needed(); lv_scr_load(s_scr); tick(); }

static void tick() {
    bool ap = netprov_is_active();
    if (s_info) { if (ap) lv_obj_clear_flag(s_info, LV_OBJ_FLAG_HIDDEN);
                  else    lv_obj_add_flag(s_info, LV_OBJ_FLAG_HIDDEN); }
    if (s_list) { if (ap) lv_obj_add_flag(s_list, LV_OBJ_FLAG_HIDDEN);
                  else    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN); }

    // Refresh the list from the client task's discovered set.
    if (!ap) {
        std::vector<jmri_discovery::JmriServer> latest = withr::get_server_choices();
        if (latest.size() != s_shown.size()) { s_shown = latest; rebuild_rows(); }
    }
}

ScreenModule g_screen_provisioning{ enter, nullptr, tick, nullptr };
