#include "screen_mgr.h"
#include "withrottle_client.h"
#include "ui.h"
#include "battery_bsp.h"
#include "esp_timer.h"

#include <stdio.h>
#include <optional>
#include <string>

// ---- Dynamic objects (built once, kept for the screen's lifetime) ----------
static lv_obj_t* s_horn_btn      = nullptr;
static lv_obj_t* s_battery_label = nullptr;
static lv_obj_t* s_speed_label   = nullptr;   // speed % readout, below the train ID

// Timestamp (us) of the last encoder speed change. While recent, the knob is
// the authority for the arc and tick() must not overwrite it from get_speed()
// (the command hasn't round-tripped to JMRI yet, so it would snap back to 0).
static int64_t s_last_speed_input_us = 0;

// ---- Styling helpers (ported from ui_events.cpp) ---------------------------

// Apply uniform circular style with a specific colour to a button.
static void style_circle_btn(lv_obj_t* btn, uint32_t colour, uint32_t colour_pressed)
{
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(colour),         LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn,   255,                          LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(colour_pressed), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn,   255,                          LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn,  2, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// Apply uniform label style (white, montserrat 20).
static void style_btn_label(lv_obj_t* label)
{
    lv_obj_set_style_text_font(label,  &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void update_battery_label(int pct)
{
    if (!s_battery_label) return;

    char text[24];
    lv_color_t col;

    if (battery_is_charging()) {
        snprintf(text, sizeof(text), LV_SYMBOL_CHARGE " %d%%", pct);
        col = lv_color_hex(0x44CC44);
    } else {
        const char* sym = (pct > 75) ? LV_SYMBOL_BATTERY_FULL :
                          (pct > 50) ? LV_SYMBOL_BATTERY_3    :
                          (pct > 25) ? LV_SYMBOL_BATTERY_2    :
                          (pct > 10) ? LV_SYMBOL_BATTERY_1    :
                                       LV_SYMBOL_BATTERY_EMPTY;
        snprintf(text, sizeof(text), "%s %d%%", sym, pct);
        col = (pct > 25) ? lv_color_hex(0xCCCCCC) :
              (pct > 10) ? lv_color_hex(0xFFAA00) :
                           lv_color_hex(0xFF3333);
    }

    lv_label_set_text(s_battery_label, text);
    lv_obj_set_style_text_color(s_battery_label, col, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// ---- Event handlers --------------------------------------------------------

static void on_fn(lv_event_t*)   { screen_mgr_show(Screen::FUNCTIONS); }
static void on_back(lv_event_t*) { withr::release_loco(); screen_mgr_show(Screen::HOME); }

static void on_horn(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    // PRESSED = touch down, RELEASED/PRESS_LOST = touch up or finger moved off
    withr::horn(code == LV_EVENT_PRESSED);
}

// ---- Module --------------------------------------------------------------

static void enter()
{
    lv_scr_load(ui_Train_Main_Control);

    static bool s_built = false;
    if (s_built || ui_Train_Main_Control == nullptr) return;
    s_built = true;

    // ---- Back button: replace PNG arrow with house symbol ----
    if (ui_Train_Main_Control_Back_Btn)
    {
        lv_obj_set_style_bg_img_opa(ui_Train_Main_Control_Back_Btn, 0,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t* home_lbl = lv_label_create(ui_Train_Main_Control_Back_Btn);
        lv_label_set_text(home_lbl, LV_SYMBOL_HOME);
        lv_obj_set_style_text_font(home_lbl, &lv_font_montserrat_26,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(home_lbl, lv_color_hex(0xE3E3E3),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(home_lbl);

        // Rebind: drop generated cb (→ Main_Screen) and release loco + go Home.
        lv_obj_remove_event_cb(ui_Train_Main_Control_Back_Btn,
                               ui_event_Train_Main_Control_Back_Btn);
        lv_obj_add_event_cb(ui_Train_Main_Control_Back_Btn, on_back,
                            LV_EVENT_CLICKED, nullptr);
    }

    // ---- Uniform 80x80 circular buttons at x = -100 / 0 / +100 from centre ----
    // STOP: red. Generated cb stays → onStopClicked() in ui_events.cpp.
    lv_obj_set_size(ui_Train_Main_Stop_Btn, 80, 80);
    lv_obj_set_x(ui_Train_Main_Stop_Btn, -100);
    style_circle_btn(ui_Train_Main_Stop_Btn, 0xC0392B, 0xE74C3C);
    style_btn_label(ui_Train_Main_Stop_Btn_Label1);

    // Fn: green. Rebind: drop generated cb (→ Fn_Control) and route via screen_mgr.
    lv_obj_set_size(ui_Train_Main_Fn_Btn, 80, 80);
    lv_obj_set_x(ui_Train_Main_Fn_Btn, 100);
    style_circle_btn(ui_Train_Main_Fn_Btn, 0x1A6B35, 0x27AE60);
    style_btn_label(ui_Train_Main_Fn_Btn_Label);
    lv_label_set_text(ui_Train_Main_Fn_Btn_Label, "FN");
    lv_obj_remove_event_cb(ui_Train_Main_Fn_Btn, ui_event_Train_Main_Fn_Btn);
    lv_obj_add_event_cb(ui_Train_Main_Fn_Btn, on_fn, LV_EVENT_CLICKED, nullptr);

    // ---- Horn button (centre) - blue ----
    s_horn_btn = lv_btn_create(ui_Train_Main_Control);
    lv_obj_set_size(s_horn_btn, 80, 80);
    lv_obj_align(s_horn_btn, LV_ALIGN_CENTER, 0, 25);
    style_circle_btn(s_horn_btn, 0x1A5276, 0x5DADE2);

    lv_obj_t* horn_lbl = lv_label_create(s_horn_btn);
    lv_label_set_text(horn_lbl, "HORN");
    style_btn_label(horn_lbl);
    lv_obj_center(horn_lbl);

    lv_obj_add_event_cb(s_horn_btn, on_horn, LV_EVENT_PRESSED,    nullptr);
    lv_obj_add_event_cb(s_horn_btn, on_horn, LV_EVENT_RELEASED,   nullptr);
    lv_obj_add_event_cb(s_horn_btn, on_horn, LV_EVENT_PRESS_LOST, nullptr);

    // ---- Battery label - top area, to the right of the back button ----
    s_battery_label = lv_label_create(ui_Train_Main_Control);
    lv_obj_set_style_text_font(s_battery_label, &lv_font_montserrat_12,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_battery_label, lv_color_hex(0xCCCCCC),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_battery_label, LV_SYMBOL_BATTERY_FULL " --%");
    lv_obj_align(s_battery_label, LV_ALIGN_CENTER, 70, -120);

    // ---- Speed % readout, just below the train ID (which is at y=-81) ----
    s_speed_label = lv_label_create(ui_Train_Main_Control);
    lv_obj_set_style_text_font(s_speed_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_speed_label, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(s_speed_label, "0%");
    lv_obj_align(s_speed_label, LV_ALIGN_CENTER, 0, -48);
}

static void tick()
{
    // If Wi-Fi/JMRI dropped, fall back to the connecting screen instead of
    // acting on a dead loco. The connecting screen auto-advances to HOME once READY.
    if (withr::get_phase() != withr::Phase::READY)
    {
        screen_mgr_show(Screen::CONNECTING);
        return;
    }

    // ---- Loco name ----
    static std::string s_last_name;
    if (ui_Train_Main_Name_Label)
    {
        std::optional<std::string> name = withr::get_loco_name();
        std::string next = (name && !name->empty()) ? *name : "\xe2\x80\x94";
        if (next != s_last_name)
        {
            lv_label_set_text(ui_Train_Main_Name_Label, next.c_str());
            s_last_name = next;
        }
    }

    // ---- Direction label ----
    static int s_last_dir = -1; // 0 = F, 1 = R
    if (ui_Train_Main_direction_label)
    {
        std::optional<Direction> dir = withr::get_direction();
        int cur = (!dir || *dir == Direction::Forward) ? 0 : 1;
        if (cur != s_last_dir)
        {
            lv_label_set_text(ui_Train_Main_direction_label, cur == 0 ? "F" : "R");
            s_last_dir = cur;
        }
    }

    // ---- Battery (every 30 s) ----
    static int64_t s_last_batt_us = -30000000; // force an immediate update
    int64_t now = esp_timer_get_time();
    if (now - s_last_batt_us >= 30000000)
    {
        update_battery_label(battery_get_pct());
        s_last_batt_us = now;
    }

    // ---- Reflect external (JMRI-side) speed onto the arc ----
    // Skip while the user is actively driving with the knob: their input owns
    // the arc, and get_speed() still reads the pre-command value until JMRI
    // echoes it, which would snap the arc back to 0.
    if (ui_Train_Main_Throttle &&
        (esp_timer_get_time() - s_last_speed_input_us) > 1500000)
    {
        std::optional<uint8_t> pct = withr::get_speed();
        if (pct)
        {
            int16_t mn = lv_arc_get_min_value(ui_Train_Main_Throttle);
            int16_t mx = lv_arc_get_max_value(ui_Train_Main_Throttle);
            int16_t nv = (int16_t)(mn + ((int)*pct * (mx - mn)) / 100);
            lv_arc_set_value(ui_Train_Main_Throttle, nv);
        }
    }

    // ---- Speed % readout — derived from the arc (driven by knob + get_speed) ----
    if (s_speed_label && ui_Train_Main_Throttle)
    {
        int16_t v   = lv_arc_get_value(ui_Train_Main_Throttle);
        int16_t mn  = lv_arc_get_min_value(ui_Train_Main_Throttle);
        int16_t mx  = lv_arc_get_max_value(ui_Train_Main_Throttle);
        int     pct = (mx > mn) ? ((int) (v - mn) * 100) / (mx - mn) : 0;
        static int s_last_pct = -1;
        if (pct != s_last_pct)
        {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", pct);
            lv_label_set_text(s_speed_label, buf);
            s_last_pct = pct;
        }
    }
}

static void on_rotate(int delta)
{
    if (!ui_Train_Main_Throttle) return;
    int16_t cur = lv_arc_get_value(ui_Train_Main_Throttle);
    int16_t mn  = lv_arc_get_min_value(ui_Train_Main_Throttle);
    int16_t mx  = lv_arc_get_max_value(ui_Train_Main_Throttle);
    int16_t nv  = cur + (-1 * (delta / 50));
    if (nv < mn) nv = mn;
    if (nv > mx) nv = mx;
    lv_arc_set_value(ui_Train_Main_Throttle, nv);
    withr::set_speed((uint8_t)(((nv - mn) * 100) / (mx - mn)));
    s_last_speed_input_us = esp_timer_get_time();
}

ScreenModule g_screen_throttle{ enter, nullptr, tick, on_rotate };
