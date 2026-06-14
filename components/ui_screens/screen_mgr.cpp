#include "screen_mgr.h"
#include <cstddef>

// Each screen file defines and registers its module via this table.
extern ScreenModule g_screen_splash;
extern ScreenModule g_screen_connecting;
extern ScreenModule g_screen_home;
extern ScreenModule g_screen_roster;
extern ScreenModule g_screen_recent;
extern ScreenModule g_screen_throttle;
extern ScreenModule g_screen_functions;
extern ScreenModule g_screen_settings;
extern ScreenModule g_screen_provisioning;

static ScreenModule* module_for(Screen s) {
    switch (s) {
        case Screen::SPLASH:     return &g_screen_splash;
        case Screen::CONNECTING: return &g_screen_connecting;
        case Screen::HOME:       return &g_screen_home;
        case Screen::ROSTER:     return &g_screen_roster;
        case Screen::RECENT:     return &g_screen_recent;
        case Screen::THROTTLE:   return &g_screen_throttle;
        case Screen::FUNCTIONS:  return &g_screen_functions;
        case Screen::SETTINGS:     return &g_screen_settings;
        case Screen::PROVISIONING: return &g_screen_provisioning;
    }
    return nullptr;
}

static Screen s_current = Screen::SPLASH;

void screen_mgr_init() {
    s_current = Screen::SPLASH;
    ScreenModule* m = module_for(s_current);
    if (m && m->enter) m->enter();
}

void screen_mgr_show(Screen s) {
    if (s == s_current) return;
    ScreenModule* prev = module_for(s_current);
    if (prev && prev->leave) prev->leave();
    s_current = s;
    ScreenModule* next = module_for(s_current);
    if (next && next->enter) next->enter();
}

Screen screen_mgr_current() { return s_current; }

void screen_mgr_tick() {
    ScreenModule* m = module_for(s_current);
    if (m && m->tick) m->tick();
}

void screen_mgr_on_rotate(int delta) {
    ScreenModule* m = module_for(s_current);
    if (m && m->on_rotate) m->on_rotate(delta);
}
