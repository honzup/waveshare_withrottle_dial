#include "screen_mgr.h"
#include "train_fn_screen.h"
#include "withrottle_client.h"
#include "list_widget.h"
#include "ui.h"
#include "esp_timer.h"

static void go_throttle(lv_event_t*) { screen_mgr_show(Screen::THROTTLE); }

static int s_focus = 0;  // active (highlighted) function row

static void enter()
{
    lv_scr_load(ui_Train_Fn_Control);

    static bool s_built = false;
    if (!s_built && ui_Select_Train_Back_Btn1)
    {
        // Rebind: drop generated cb (→ Train_Main_Control) and route via screen_mgr.
        lv_obj_remove_event_cb(ui_Select_Train_Back_Btn1, ui_event_Select_Train_Back_Btn1);
        lv_obj_add_event_cb(ui_Select_Train_Back_Btn1, go_throttle, LV_EVENT_CLICKED, nullptr);

        // Pin the title bar (back button + loco name) to the top of the screen.
        // It is generated as the first child of the scrollable container, so it
        // scrolled off with the function list. Reparenting it onto the screen
        // keeps it fixed AND means the focus highlight no longer counts it as a
        // list item (the container now holds only function rows). An opaque bar
        // hides rows that scroll up beneath it; container top padding clears it.
        if (ui_Select_Train_Title1)
        {
            lv_obj_set_parent(ui_Select_Train_Title1, ui_Train_Fn_Control);
            lv_obj_align(ui_Select_Train_Title1, LV_ALIGN_TOP_MID, 0, 8);
            lv_obj_set_style_bg_color(ui_Select_Train_Title1, lv_color_hex(0x000000),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(ui_Select_Train_Title1, LV_OPA_COVER,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
            if (uic_train_fn_container)
                lv_obj_set_style_pad_top(uic_train_fn_container, 56,
                                         LV_PART_MAIN | LV_STATE_DEFAULT);
            // Nudge the back arrow inward: at x=-91 it sat under the curve of
            // the round display and was clipped on the left.
            lv_obj_set_x(ui_Select_Train_Back_Btn1, -70);
        }
        s_built = true;
    }

    TrainFnScreen::load_for_loco();
    // Highlight the active function row (the rows wrap a button -> nested=true).
    s_focus = list_widget_focus(uic_train_fn_container, s_focus, 0, true);
}

// NB: no leave() handler. We must NOT delete the function rows here — leave()
// runs inside the LVGL event dispatch of the back-button click, and deleting
// objects mid-event corrupts LVGL's event/indev state (LoadProhibited crash).
// The 32 rows are built once and live in PSRAM; load_for_loco() refreshes their
// labels/visibility/state on each entry, so they're safely reused.

static uint32_t s_last_refresh = 0;

static void tick()
{
    // If Wi-Fi/JMRI dropped, fall back to the connecting screen instead of
    // acting on a dead loco. The connecting screen auto-advances to HOME once READY.
    if (withr::get_phase() != withr::Phase::READY)
    {
        screen_mgr_show(Screen::CONNECTING);
        return;
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_last_refresh >= 250)  // refresh often so button colour tracks JMRI state
    {
        TrainFnScreen::load_for_loco();
        // Re-apply focus: a loco change may have hidden/shown rows.
        s_focus = list_widget_focus(uic_train_fn_container, s_focus, 0, true);
        s_last_refresh = now;
    }
}

static void on_rotate(int delta)
{
    if (!uic_train_fn_container) return;
    s_focus = list_widget_focus(uic_train_fn_container, s_focus, delta, true);
}

ScreenModule g_screen_functions{ enter, nullptr, tick, on_rotate };
