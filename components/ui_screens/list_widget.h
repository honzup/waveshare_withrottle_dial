#pragma once
#include "lvgl.h"
#include "loco_ref.h"
#include <vector>

// Rebuild `container`'s children as one button per loco. on_select is called
// with the chosen LocoRef when a row is tapped. Returns nothing; clears existing
// children first. container must be a scrollable flex-column.
void list_widget_populate(lv_obj_t* container,
                          const std::vector<LocoRef>& items,
                          void (*on_select)(const LocoRef&));

// Move the "active" focus highlight (a gold outline) among `container`'s
// non-hidden children by the encoder `delta` (scroll-accumulator units, ±50 per
// detent; pass 0 to just (re)apply the current focus), and scroll it into view.
// Returns the new focus index. If `nested` is true the outline is applied to
// each child's first child (used by the function list, whose rows wrap a button).
int list_widget_focus(lv_obj_t* container, int focus, int delta, bool nested);
