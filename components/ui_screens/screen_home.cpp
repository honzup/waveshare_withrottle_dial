#include "screen_mgr.h"
#include "ui.h"
#include "lvgl.h"
#include "battery_bsp.h"
#include "esp_timer.h"
#include <stdio.h>

static void go_roster(lv_event_t*)   { screen_mgr_show(Screen::ROSTER); }
static void go_recent(lv_event_t*)   { screen_mgr_show(Screen::RECENT); }
static void go_settings(lv_event_t*) { screen_mgr_show(Screen::SETTINGS); }

static bool      s_wired      = false;
static lv_obj_t* s_batt       = nullptr;  // battery readout at the top
static uint32_t  s_last_batt  = 0;

// Refresh the battery label: a level symbol (or charge bolt) + percentage.
static void update_battery() {
    if (!s_batt) return;
    int pct = battery_get_pct();
    char buf[24];
    if (battery_is_charging()) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %d%%", pct);
    } else {
        const char* sym = LV_SYMBOL_BATTERY_EMPTY;
        if      (pct >= 90) sym = LV_SYMBOL_BATTERY_FULL;
        else if (pct >= 65) sym = LV_SYMBOL_BATTERY_3;
        else if (pct >= 40) sym = LV_SYMBOL_BATTERY_2;
        else if (pct >= 15) sym = LV_SYMBOL_BATTERY_1;
        snprintf(buf, sizeof(buf), "%s %d%%", sym, pct);
    }
    lv_label_set_text(s_batt, buf);
}

static void enter() {
    if (!s_wired) {
        // Three equally-spaced menu buttons (matching the generated style):
        // Roster (top), Recents (middle), Settings (bottom).
        const int BTN_W = 380, BTN_H = 80;

        // --- Roster (was "Run Train" / Select_Train_Btn) ---
        // Drop the generated cb (it wrongly targeted Train_Main_Control), rebind
        // to the roster, rename, and reposition.
        lv_obj_remove_event_cb(ui_Select_Train_Btn, ui_event_Select_Train_Btn);
        lv_obj_add_event_cb(ui_Select_Train_Btn, go_roster, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(ui_Select_Train_Btn_Label, "Roster");
        lv_obj_align(ui_Select_Train_Btn, LV_ALIGN_CENTER, 0, -100);
        lv_obj_align(ui_Select_Train_Btn_Label, LV_ALIGN_CENTER, 0, 0);

        // --- Recents (new) — built to match the generated buttons ---
        lv_obj_t* recents_btn = lv_btn_create(ui_Main_Screen);
        lv_obj_set_size(recents_btn, BTN_W, BTN_H);
        lv_obj_align(recents_btn, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(recents_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(recents_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);          // transparent
        lv_obj_set_style_bg_color(recents_btn, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(recents_btn, 150, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_t* recents_lbl = lv_label_create(recents_btn);
        lv_label_set_text(recents_lbl, "Recent");
        ui_object_set_themeable_style_property(recents_lbl, LV_PART_MAIN | LV_STATE_DEFAULT,
                                               LV_STYLE_TEXT_COLOR, _ui_theme_color_Standout);
        ui_object_set_themeable_style_property(recents_lbl, LV_PART_MAIN | LV_STATE_DEFAULT,
                                               LV_STYLE_TEXT_OPA, _ui_theme_alpha_Standout);
        lv_obj_set_style_text_font(recents_lbl, &lv_font_montserrat_40, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(recents_lbl);
        lv_obj_add_event_cb(recents_btn, go_recent, LV_EVENT_CLICKED, nullptr);

        // --- Settings (bottom) ---
        lv_obj_remove_event_cb(ui_Settings_Btn, ui_event_Settings_Btn);
        lv_obj_add_event_cb(ui_Settings_Btn, go_settings, LV_EVENT_CLICKED, nullptr);
        lv_obj_align(ui_Settings_Btn, LV_ALIGN_CENTER, 0, 100);
        lv_obj_align(ui_Settings_Btn_Label, LV_ALIGN_CENTER, 0, 0);

        // --- Battery readout (top, above the Roster button) ---
        s_batt = lv_label_create(ui_Main_Screen);
        lv_obj_set_style_text_font(s_batt, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_batt, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(s_batt, LV_SYMBOL_BATTERY_FULL " --%");
        lv_obj_align(s_batt, LV_ALIGN_TOP_MID, 0, 24);

        s_wired = true;
    }
    lv_scr_load(ui_Main_Screen);
    s_last_batt = 0;     // force an immediate refresh on entry
    update_battery();
}

static void tick() {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_last_batt < 3000) return;   // refresh every ~3 s
    s_last_batt = now;
    update_battery();
}

ScreenModule g_screen_home{ enter, nullptr, tick, nullptr };
