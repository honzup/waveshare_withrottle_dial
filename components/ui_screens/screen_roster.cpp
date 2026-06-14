#include "screen_mgr.h"
#include "withrottle_client.h"
#include "recents.h"
#include "list_widget.h"
#include "ui.h"

// Dedicated list container — created once, separate from the generated
// ui_Train_Select_Container (which holds the title + back button, so we must
// never let list_widget_populate clean it).
static lv_obj_t* s_list = nullptr;
static bool      s_wired = false;
static uint32_t  s_last_count = 0;
static int       s_focus = 0;   // active (highlighted) row index

static void on_pick(const LocoRef& r) {
    withr::acquire_loco(r.address, r.length);
    recents_store_push(r);
    screen_mgr_show(Screen::THROTTLE);
}

static void go_home(lv_event_t*) { screen_mgr_show(Screen::HOME); }

static void enter() {
    lv_scr_load(ui_Select_Train_Screen);

    if (!s_wired) {
        // Hide the generated dummy row AND the title bar. The title bar holds the
        // generated back button, but it's a flex child of a centre-aligned
        // container — once the dummy row is hidden, flex centres it to the middle
        // of the screen, under our list (so the back button was unreachable). We
        // provide our own top bar instead.
        if (ui_Select_Trains_Panel) lv_obj_add_flag(ui_Select_Trains_Panel, LV_OBJ_FLAG_HIDDEN);
        if (ui_Select_Train_Title)  lv_obj_add_flag(ui_Select_Train_Title, LV_OBJ_FLAG_HIDDEN);

        // List container, filling the area below the top bar.
        s_list = lv_obj_create(ui_Select_Train_Screen);
        lv_obj_remove_style_all(s_list);
        lv_obj_set_size(s_list, 340, 280);
        lv_obj_align(s_list, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_bg_opa(s_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(s_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        // Top bar (within the round display): home button left-of-centre + title.
        // Positioned via CENTER offsets — the top-LEFT corner is outside the
        // 360px circle, so a corner-aligned button would be off-screen.
        lv_obj_t* home_btn = lv_btn_create(ui_Select_Train_Screen);
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

        lv_obj_t* title = lv_label_create(ui_Select_Train_Screen);
        lv_label_set_text(title, "Roster");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(title, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(title, LV_ALIGN_CENTER, 20, -138);

        s_wired = true;
    }
    // Note: do NOT force a repopulate here. tick() rebuilds only when the roster
    // size actually changes (entries streaming in). Rebuilding the 48-button list
    // on every entry is wasteful and repeatedly destroys/recreates LVGL objects
    // the input device may reference. The list persists (in PSRAM) and is reused.
}

static void tick() {
    if (!s_list) return;
    // Repopulate when the roster size changes (entries stream in after connect).
    auto roster = withr::get_roster();
    if (roster.size() != s_last_count) {
        s_last_count = roster.size();
        list_widget_populate(s_list, roster, on_pick);
        if (s_focus >= (int) roster.size()) s_focus = 0;
        s_focus = list_widget_focus(s_list, s_focus, 0, false);  // re-apply highlight
    }
}

static void on_rotate(int delta) {
    if (!s_list) return;
    s_focus = list_widget_focus(s_list, s_focus, delta, false);
}

ScreenModule g_screen_roster{ enter, nullptr, tick, on_rotate };
