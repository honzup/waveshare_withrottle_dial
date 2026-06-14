#include "list_widget.h"
#include "ui.h"   // theme colours / fonts
#include <new>
#include <string>

// Per-container state. Each list container (roster, recent) owns its own
// callback + item copy, attached via the container's user_data. Module globals
// were shared between both screens, so whichever populated last won — visiting
// recent then returning to roster (which only repopulates on a size change)
// left the callback/items pointing at recent, dropping every roster tap.
struct ListCtx {
    void (*on_select)(const LocoRef&) = nullptr;
    std::vector<LocoRef> items; // copy so the callback can read it after the click
};

static void row_clicked(lv_event_t* e) {
    intptr_t  idx  = (intptr_t) lv_event_get_user_data(e);
    lv_obj_t* btn  = lv_event_get_current_target(e);          // the row button
    lv_obj_t* cont = btn ? lv_obj_get_parent(btn) : nullptr;  // its list container
    ListCtx*  ctx  = cont ? (ListCtx*) lv_obj_get_user_data(cont) : nullptr;
    if (ctx && ctx->on_select && idx >= 0 && idx < (intptr_t) ctx->items.size())
        ctx->on_select(ctx->items[idx]);
}

// Populate by REUSING existing child buttons rather than deleting and
// recreating them. Deleting LVGL objects (lv_obj_clean/lv_obj_del) repeatedly
// proved fragile here — it crashed in _lv_event_mark_deleted (corrupted event
// state) whether called from an event or the UI task. Button at child index i
// always represents items[i] (and s_items[i]), so the click user_data (= i) is
// stable across repopulations; we only relabel and show/hide.
void list_widget_populate(lv_obj_t* container,
                          const std::vector<LocoRef>& items,
                          void (*on_select)(const LocoRef&)) {
    if (!container) return;
    // Bind this container's own callback + items (created once, reused).
    ListCtx* ctx = (ListCtx*) lv_obj_get_user_data(container);
    if (!ctx) { ctx = new (std::nothrow) ListCtx(); lv_obj_set_user_data(container, ctx); }
    if (ctx) { ctx->on_select = on_select; ctx->items = items; }

    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    // Stack from the top, centred horizontally, with a gap between rows and a
    // little top padding so the first row clears the top bar on opening.
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(container, 14, LV_PART_MAIN | LV_STATE_DEFAULT);

    uint32_t existing = lv_obj_get_child_cnt(container);

    for (size_t i = 0; i < items.size(); ++i) {
        lv_obj_t* btn;
        lv_obj_t* lbl;
        if (i < existing) {
            // Reuse the existing button/label at this position.
            btn = lv_obj_get_child(container, i);
            lbl = lv_obj_get_child(btn, 0);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Grow the pool: create a new button (never deleted afterwards).
            btn = lv_btn_create(container);
            lv_obj_set_size(btn, 240, 56);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C4A7E), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x3E66A8), LV_PART_MAIN | LV_STATE_PRESSED);
            lbl = lv_label_create(btn);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_center(lbl);
            lv_obj_add_event_cb(btn, row_clicked, LV_EVENT_CLICKED, (void*) (intptr_t) i);
        }

        // Show the bare DCC address (no 'L'/'S' length prefix).
        const LocoRef& r = items[i];
        std::string    addr = std::to_string(r.address);
        std::string    text = r.name.empty() ? addr : r.name + "  (" + addr + ")";
        lv_label_set_text(lbl, text.c_str());
    }

    // Hide any surplus buttons left over from a longer previous list.
    for (uint32_t i = items.size(); i < existing; ++i) {
        lv_obj_add_flag(lv_obj_get_child(container, i), LV_OBJ_FLAG_HIDDEN);
    }
}

int list_widget_focus(lv_obj_t* container, int focus, int delta, bool nested) {
    if (!container) return focus;
    const int n = (int) lv_obj_get_child_cnt(container);
    if (n == 0) return 0;

    auto hidden = [&](int i) {
        return lv_obj_has_flag(lv_obj_get_child(container, i), LV_OBJ_FLAG_HIDDEN);
    };

    int idx = focus;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    // If the current focus landed on a hidden child, snap to the first visible.
    if (hidden(idx)) { idx = 0; while (idx < n && hidden(idx)) ++idx; }
    if (idx >= n) return focus;  // nothing visible

    // Move by whole detents, skipping hidden children, clamping at the ends.
    // Negated so a clockwise turn moves the highlight DOWN the list.
    int steps = -delta / 50;
    if (delta != 0 && steps == 0) steps = (delta > 0) ? -1 : 1;
    const int dir = (steps >= 0) ? 1 : -1;
    for (int rem = (steps >= 0) ? steps : -steps; rem > 0; --rem) {
        int next = idx + dir;
        while (next >= 0 && next < n && hidden(next)) next += dir;
        if (next < 0 || next >= n) break;
        idx = next;
    }

    // Apply the outline to the focused item (its inner button if nested), clear
    // it on all others, and scroll the focused child into view.
    for (int i = 0; i < n; ++i) {
        lv_obj_t* child = lv_obj_get_child(container, i);
        lv_obj_t* tgt   = nested ? lv_obj_get_child(child, 0) : child;
        if (!tgt) continue;
        lv_obj_set_style_outline_width(tgt, (i == idx) ? 3 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        if (i == idx) {
            lv_obj_set_style_outline_color(tgt, lv_color_hex(0xF1C40F), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_outline_opa(tgt, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_outline_pad(tgt, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    // Scroll the row one PAST the focused one (in the travel direction) into
    // view, so the active row settles one row in from the edge with a row of
    // lookahead below (or above) it, rather than pinned to the very bottom.
    lv_obj_t* anchor = lv_obj_get_child(container, idx);
    int       look   = idx + dir;
    if (look >= 0 && look < n &&
        !lv_obj_has_flag(lv_obj_get_child(container, look), LV_OBJ_FLAG_HIDDEN)) {
        anchor = lv_obj_get_child(container, look);
    }
    lv_obj_scroll_to_view(anchor, LV_ANIM_ON);
    return idx;
}
