/*
 * ui_events.cpp — callbacks the SquareLine-generated UI invokes by name.
 *
 * Most per-screen behaviour now lives in the ui_screens component, which binds
 * its own handlers in each screen's enter() routine. The generated code still
 * references these symbols, so they stay defined here: the *_Loaded / *_Unloaded
 * hooks are no-ops, while the STOP and direction buttons (which the screen
 * modules do not rebind) keep working bodies. The linker is told to keep
 * onStopClicked via -Wl,-u,onStopClicked in main/CMakeLists.txt.
 *
 * Independently authored for waveshare_withrottle_dial.
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#include "withrottle_client.h"
#include "ui.h"

extern "C" {

void onStopClicked(lv_event_t * /*e*/)
{
    withr::emergency_stop();
}

void onDirClicked(lv_event_t * /*e*/)
{
    // Flip the current direction, if we know it.
    if (auto dir = withr::get_direction()) {
        withr::set_direction(*dir == Direction::Forward ? Direction::Reverse : Direction::Forward);
    }
}

void onTrainMainControlLoaded(lv_event_t * /*e*/) {}
void onTrainFnControlLoaded(lv_event_t * /*e*/) {}
void onTrainFnUnloaded(lv_event_t * /*e*/) {}

}  // extern "C"
