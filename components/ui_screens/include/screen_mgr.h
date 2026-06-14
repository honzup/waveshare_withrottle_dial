#pragma once

enum class Screen { SPLASH, CONNECTING, HOME, ROSTER, RECENT, THROTTLE, FUNCTIONS, SETTINGS, PROVISIONING };

// One screen module. Any callback may be null.
struct ScreenModule {
    void (*enter)();              // activate/refresh (lv_scr_load + refresh)
    void (*leave)();              // detach handlers / stop timers
    void (*tick)();               // ~50 ms, called under the LVGL lock
    void (*on_rotate)(int delta); // encoder rotation (already LVGL-locked)
};

void   screen_mgr_init();                 // registers modules; shows CONNECTING
void   screen_mgr_show(Screen s);
Screen screen_mgr_current();
void   screen_mgr_tick();                 // dispatch: active->tick()
void   screen_mgr_on_rotate(int delta);   // dispatch: active->on_rotate()
