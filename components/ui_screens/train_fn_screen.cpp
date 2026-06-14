#include "train_fn_screen.h"

#include "ui.h"

#include <string>

// ---- Static member definitions --------------------------------------------

lv_obj_t* TrainFnScreen::s_fn_rows[MAX_FUNCTIONS] = {};
lv_obj_t* TrainFnScreen::s_fn_btns[MAX_FUNCTIONS] = {};
bool      TrainFnScreen::s_buttons_created        = false;

// ---- Private helpers ------------------------------------------------------

void TrainFnScreen::on_fn_btn_event(lv_event_t* e)
{
    uint8_t func = (uint8_t) (uintptr_t) lv_event_get_user_data(e);

    // Press/release model, like a real WiThrottle throttle: send F1 on press,
    // F0 on release (or press-lost). JMRI handles the rest — it toggles a
    // latching function on the press and holds a momentary one while pressed —
    // and reports the resulting state back, which drives the button colour
    // (set in load_for_loco). The app needs no momentary/latching knowledge.
    bool pressed = (lv_event_get_code(e) == LV_EVENT_PRESSED);
    withr::set_function(func, pressed);
}

// ---- Public API -----------------------------------------------------------

void TrainFnScreen::load_for_loco()
{
    // Guard: container must be valid (set by generated screen init)
    if (uic_train_fn_container == nullptr)
        return;

    // --- Update loco name label ---
    if (ui_Fn_Train_Name != nullptr)
    {
        std::optional<std::string> name = withr::get_loco_name();
        lv_label_set_text(ui_Fn_Train_Name, name ? name->c_str() : "\xe2\x80\x94");
    }

    // --- Create buttons on first load ---
    if (!s_buttons_created)
    {
        // The generated screen clears SCROLLABLE; restore it so the rotary
        // encoder scroll handler in main.cpp can drive lv_obj_scroll_by().
        lv_obj_add_flag(uic_train_fn_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(uic_train_fn_container, LV_SCROLLBAR_MODE_OFF);

        for (uint8_t i = 0; i < MAX_FUNCTIONS; i++)
        {
            // Row panel
            lv_obj_t* row = lv_obj_create(uic_train_fn_container);
            lv_obj_set_size(row, 360, 50);
            lv_obj_set_align(row, LV_ALIGN_CENTER);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_opa(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_opa(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

            // Function button
            lv_obj_t* btn = lv_btn_create(row);
            lv_obj_set_size(btn, 280, 44);
            lv_obj_set_align(btn, LV_ALIGN_CENTER);
            // Not CHECKABLE: the on/off (CHECKED) state is set programmatically
            // from JMRI's reported function state, not toggled locally by taps.
            lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

            // Off appearance — the same blue as the roster/recent list rows.
            lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C4A7E), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

            // Checked (on) appearance — distinct green so active functions are
            // obvious against the blue off-state.
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x27AE60), LV_PART_MAIN | LV_STATE_CHECKED);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);

            // Label
            lv_obj_t* label = lv_label_create(btn);
            lv_obj_set_align(label, LV_ALIGN_CENTER);
            lv_obj_set_width(label, LV_SIZE_CONTENT);
            lv_obj_set_height(label, LV_SIZE_CONTENT);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_20,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(label, "");

            // Press/release callbacks — pass function index as user_data.
            lv_obj_add_event_cb(btn, on_fn_btn_event, LV_EVENT_PRESSED,    (void*) (uintptr_t) i);
            lv_obj_add_event_cb(btn, on_fn_btn_event, LV_EVENT_RELEASED,   (void*) (uintptr_t) i);
            lv_obj_add_event_cb(btn, on_fn_btn_event, LV_EVENT_PRESS_LOST, (void*) (uintptr_t) i);

            s_fn_rows[i] = row;
            s_fn_btns[i] = btn;
        }

        s_buttons_created = true;
    }

    // --- Refresh visibility, labels, and toggle state ---
    for (uint8_t i = 0; i < MAX_FUNCTIONS; i++)
    {
        lv_obj_t* row = s_fn_rows[i];
        lv_obj_t* btn = s_fn_btns[i];
        if (row == nullptr || btn == nullptr)
            continue;

        std::optional<std::string> fn_name  = withr::get_function_name(i);
        bool                       has_name = fn_name && !fn_name->empty();

        if (!has_name)
        {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);

        // Update label (first child of button)
        lv_obj_t* label = lv_obj_get_child(btn, 0);
        if (label != nullptr)
            lv_label_set_text(label, fn_name->c_str());

        // Reflect JMRI's reported function state — the server is authoritative
        // (it manages latching vs momentary; we just mirror the result).
        std::optional<bool> state = withr::get_function_state(i);
        if (state && *state)
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(btn, LV_STATE_CHECKED);
    }
}

void TrainFnScreen::on_screen_unloaded()
{
    // The screen uses auto_del=false so the object survives unload and is
    // reused on the next visit.  Delete the dynamic rows now so the container
    // is empty when load_for_loco() runs again.
    for (int i = 0; i < MAX_FUNCTIONS; i++)
    {
        if (s_fn_rows[i] != nullptr)
            lv_obj_del(s_fn_rows[i]); // also deletes the child button
        s_fn_rows[i] = nullptr;
        s_fn_btns[i] = nullptr;
    }
    s_buttons_created = false;
}
