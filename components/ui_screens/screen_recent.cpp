#include "screen_mgr.h"
#include "withrottle_client.h"
#include "recents.h"
#include "recents_serialize.h"
#include "list_widget.h"
#include "lvgl.h"
#include <string>

static lv_obj_t*   s_scr       = nullptr;
static lv_obj_t*   s_container = nullptr;
static std::string s_populated_sig; // signature of the recents currently shown
static int         s_focus = 0;     // active (highlighted) row index

static void on_pick(const LocoRef& r) {
    withr::acquire_loco(r.address, r.length);
    recents_store_push(r);
    screen_mgr_show(Screen::THROTTLE);
}

static void go_home(lv_event_t*) { screen_mgr_show(Screen::HOME); }

static void enter() {
    if (!s_scr) {
        s_scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);

        // List container, below the top bar.
        s_container = lv_obj_create(s_scr);
        lv_obj_set_size(s_container, 340, 280);
        lv_obj_align(s_container, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_flex_flow(s_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scroll_dir(s_container, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(s_container, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_bg_opa(s_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(s_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Top bar (within the round display): home button left-of-centre + title.
        lv_obj_t* home_btn = lv_btn_create(s_scr);
        lv_obj_set_size(home_btn, 50, 50);
        lv_obj_align(home_btn, LV_ALIGN_CENTER, -70, -138);
        lv_obj_set_style_radius(home_btn, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(home_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(home_btn, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(home_btn, 150, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_t* home_lbl = lv_label_create(home_btn);
        lv_label_set_text(home_lbl, LV_SYMBOL_HOME);
        lv_obj_set_style_text_font(home_lbl, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(home_lbl, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(home_lbl);
        lv_obj_add_event_cb(home_btn, go_home, LV_EVENT_CLICKED, nullptr);

        lv_obj_t* title = lv_label_create(s_scr);
        lv_label_set_text(title, "Recent");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(title, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(title, LV_ALIGN_CENTER, 20, -138);
    }

    lv_scr_load(s_scr);
    // NB: do NOT populate the list here. enter() runs inside the Recents
    // button's click-event dispatch, and list_widget_populate() deletes the old
    // buttons (lv_obj_clean) — deleting objects mid-event corrupts LVGL's event
    // stack (LoadProhibited crash). Population happens in tick() (UI task), a
    // safe non-event context, and only when the recents list has changed.
}

static void tick() {
    if (!s_container) return;
    // Rebuild only when the recents list actually changed (cheap; <=5 entries).
    std::vector<LocoRef> recents = recents_load();
    std::string          sig     = recents_serialize(recents);
    if (sig != s_populated_sig) {
        s_populated_sig = sig;
        list_widget_populate(s_container, recents, on_pick);
        if (s_focus >= (int) recents.size()) s_focus = 0;
        s_focus = list_widget_focus(s_container, s_focus, 0, false);  // re-apply highlight
    }
}

static void on_rotate(int delta) {
    if (!s_container) return;
    s_focus = list_widget_focus(s_container, s_focus, delta, false);
}

ScreenModule g_screen_recent{ enter, nullptr, tick, on_rotate };
