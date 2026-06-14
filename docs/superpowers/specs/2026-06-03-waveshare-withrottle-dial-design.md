# Waveshare WiThrottle Dial — Design

**Date:** 2026-06-03
**Status:** Approved (brainstorming)
**Target hardware:** Waveshare ESP32-S3 Knob Touch LCD 1.8″ (360 × 360 round display, rotary encoder, capacitive touch)

## 1. Summary

A standalone DCC model-railway throttle for JMRI, running on the Waveshare
ESP32-S3 Knob. It connects directly to a JMRI WiThrottle server over Wi-Fi and
provides a touch + rotary-encoder interface for selecting and driving a single
locomotive.

The project is a **fork of `withrottle_dial`** (which already targets this exact
board) with the **full single-loco feature set of `m5stack_wiThrottle`** ported
onto it. The ESP-NOW proxy backend of `withrottle_dial` is removed; the device
now talks to JMRI directly.

### Reference repositories

| Repo | Role | Stack |
|---|---|---|
| `withrottle_dial` | Base — hardware, GUI style, infrastructure | ESP-IDF/CMake, LVGL 8.4, SquareLine Studio |
| `m5stack_wiThrottle` | Feature reference — roster, recents, throttle, functions, horn | PlatformIO/Arduino, LVGL 9 |

## 2. Decisions

These were settled during brainstorming and are fixed for the first
implementation:

1. **Codebase base:** fork `withrottle_dial`. Keep its ESP-IDF/CMake structure,
   Waveshare BSP drivers, and GUI style. Replace the ESP-NOW proxy with a full
   direct-to-JMRI WiThrottle client.
2. **UI authoring:** hand-code LVGL 8.4 C. The SquareLine-generated screens are
   the visual-style reference (fonts, colours, styles), not the source of truth.
3. **Feature scope:** JMRI roster browser, recents (NVS-backed), dedicated horn
   button, plus the assumed core — 270° speed arc, direction toggle, real-time
   speed %, emergency stop, and a 32-function on/off screen. **Single loco at a
   time** (no multi-throttle).
4. **Configuration:** Wi-Fi and JMRI settings in NVS, set over the serial
   console, with compile-time defaults in a git-ignored `secrets.h`. The
   on-device Settings screen stays read-only.
5. **UI architecture:** hybrid — keep the dial's networking/drivers and `withr::`
   API (extended), and replace the monolithic `ui_update_task` if/else dispatch
   with a light screen manager where each screen is its own module.

### Out of scope (YAGNI)

Multi-throttle / consisting, on-device keyboard configuration, mDNS discovery of
the JMRI server, track-power control, ESP-NOW proxy mode.

## 3. Architecture

### 3.1 Kept from `withrottle_dial` (unchanged)

- Hardware BSPs: `lcd_touch_bsp`, `lcd_bl_pwm_bsp`, `user_encoder_bsp`,
  `i2c_bsp`, `battery_bsp`, `hw_config`.
- Infrastructure: `wifi`, `param` (NVS key/value store), `console_commands`,
  `ota_server`, `udp_log`.
- The `withrottle_client` component (delegate + dedicated FreeRTOS task +
  command queue) — extended, not rewritten.
- LVGL 8.4 and the SquareLine-generated styles/fonts/colours as the visual
  reference.

### 3.2 Removed

- The `espnow_dial` proxy backend — the device now connects to JMRI directly.

### 3.3 Threading model

The UI never blocks on TCP. The UI pushes `Cmd` structs to the existing command
queue; the `withr` task owns all socket I/O. Inbound state changes arrive via
delegate callbacks and are read by the next UI tick.

```
withr task (prio 4)                    LVGL / UI tasks (prio 5)
─────────────────────────             ──────────────────────────
WiFi wait → TCP connect               ui_update_task (50 ms tick)
WiThrottle session                      → screen_mgr dispatch
drains g_cmd_queue (UI→net)             → active screen's tick()
ThrottleDelegate callbacks            encoder task → rotation events
  update cached state                 touch (LVGL indev) → button cbs
```

- Cross-task UI updates stay inside the LVGL lock (`ui_lvgl_lock`/`unlock`), as
  the dial does today.
- **State lives in one place — the `withr::` API.** Its delegate already caches
  roster, function names/states, direction and speed. The UI reads via
  `withr::get_*()` getters each tick. We do not duplicate state into a separate
  global (unlike m5's `g_state`).

### 3.4 Component layout

New or changed in **bold**.

```
components/
  withrottle_client/   ← EXTENDED: acquire/release/roster API, drop hardcoded "S3"
  ui_screens/          ← NEW: screen_mgr + one .cpp per screen
  recents/             ← NEW: NVS-backed recents (port of m5's recents)
  input/               ← NEW: encoder rotation + touch routing helper
  (wifi, param, console_commands, ota_server, lcd_*_bsp, battery_bsp, … kept)
main/
  main.cpp             ← trimmed: boot, tasks, screen_mgr_init; espnow removed
```

## 4. State model, `withr::` API and data flow

The `withrottle_client` component is the single source of truth. Today it
hardcodes `addLocomotive(DEFAULT_MULTITHROTTLE, "S3")`; we extend the public API
to support roster-driven selection.

### 4.1 New / changed `withr::` API

```cpp
// --- loco lifecycle (NEW — replaces hardcoded S3) ---
void acquire_loco(int address, char length);  // releases any current, then addLocomotive
void release_loco();                           // releaseLocomotive; returns to no-loco
bool has_loco();                               // a loco is currently acquired

// --- roster access for the UI (NEW) ---
struct RosterItem { int address; char length; std::string name; };
std::vector<RosterItem> get_roster();          // snapshot, sorted by name
bool roster_ready();                            // full roster received from JMRI

// --- connection phase for the connecting screen (NEW) ---
enum class Phase { WIFI, SERVER, ROSTER, READY };
Phase get_phase();

// --- horn (NEW) ---
int  get_horn_fn();        // resolved index (label contains "horn", else F2)

// --- existing, kept ---
// get_speed/set_speed, get_direction/set_direction, emergency_stop,
// get_function_state/name/set_function, get_loco_name, is_connected, get_server_url
```

### 4.2 Delegate / task changes

- The withr task no longer auto-acquires `"S3"`. After the session starts and
  the roster arrives (`receivedRosterEntry`), it waits for the UI to call
  `acquire_loco()`. New `CMD_ACQUIRE` / `CMD_RELEASE` command-queue entries
  (mirroring m5's `Cmd` enum) carry the request to the task.
- The delegate keys roster by DCC address in a `std::map`; `get_roster()` returns
  a sorted snapshot under a short mutex so the UI never reads a half-updated map.
- The horn index is resolved once when the active loco's function list arrives,
  using m5's `resolve_horn_fn` logic (first label containing "horn",
  case-insensitive, default 2).

### 4.3 Data flow — acquiring a loco

```
Roster screen tick reads withr::get_roster() → renders list
User taps an entry → withr::acquire_loco(addr,len)
  → pushes CMD_ACQUIRE to g_cmd_queue
withr task pops it → releaseLocomotive(old) + addLocomotive(new)
JMRI replies → delegate addressAddedMultiThrottle + function list
  → cached; get_phase()→READY, has_loco()→true
recents::push(addr,len,name)   ← UI also records the pick
screen_mgr_show(THROTTLE)
Throttle tick reads get_speed/get_direction/get_loco_name → updates arc/labels
```

Speed / direction / function / e-stop flow is unchanged from the dial: encoder
rotation or touch → `withr::set_speed/...` → `CMD_*` → task →
`WiThrottleProtocol`. Inbound (JMRI-side) changes flow back through delegate
callbacks and are picked up by the next UI tick.

## 5. Screens, navigation and input

### 5.1 Screen manager

A small module modelled on m5's `screen_mgr`:

```cpp
enum class Screen { CONNECTING, HOME, ROSTER, RECENT, THROTTLE, FUNCTIONS, SETTINGS };
void   screen_mgr_init();
void   screen_mgr_show(Screen s);
Screen screen_mgr_current();
void   screen_mgr_tick();   // called from ui_update_task under the LVGL lock
```

Each screen is its own `.cpp` exposing a uniform internal interface:

```cpp
struct ScreenModule {
    void (*enter)();             // build/refresh widgets, attach event cbs
    void (*leave)();             // detach, stop timers
    void (*tick)();              // 50 ms: poll withr:: getters
    void (*on_rotate)(int delta);// encoder rotation
};
```

- **SquareLine screens are persistent objects** (created once in `ui_init()`, as
  the dial does); `enter()` does `lv_scr_load()` + refresh rather than rebuild.
- **New hand-coded screens** (Connecting, Recent) are built in matching LVGL C
  using the SquareLine styles, created lazily on first `enter()`.

### 5.2 Screen inventory

| Screen | Source | Work |
|---|---|---|
| **Connecting** | new | Spinner + status text from `withr::get_phase()` (Wi-Fi → Server → Roster → Ready). Auto-advances to Home when `roster_ready()`. |
| **Home** | `Main_Screen` | Entry menu → Roster / Recents / Settings. Light restyle to add the Recents entry. |
| **Roster** | `Select_Train_Screen` | Replace dummy items: populate from `withr::get_roster()`, encoder scrolls, tap acquires → `acquire_loco()` + `recents::push()` → Throttle. |
| **Recent** | new | Up to 5 NVS-backed entries (most-recent first), same list style as Roster; tap acquires. |
| **Throttle** | `Train_Main_Control` | Speed arc (present) + direction toggle + STOP + HORN buttons (already styled in `ui_events.cpp`); FN button → Functions; back → release loco + Home. |
| **Functions** | `Train_Fn_Control` | Scrollable on/off list of all 32 functions from `withr::get_function_name/state`; tap toggles. Largely built. |
| **Settings** | `Settings_Screen` | Read-only: SSID / IP / server URL. |

### 5.3 Input model (touch-first — the dial's style)

- **Encoder rotation** → scrolls lists (Roster / Recent / Functions) and adjusts
  the speed arc (Throttle), via the existing `scroll_accumulator` routed to the
  active screen's `on_rotate()`.
- **Touch** → all discrete actions: select loco, direction, STOP, HORN, FN,
  back/home, settings.
- **HORN** → hold to sound: `set_function(get_horn_fn(), true)` on
  `LV_EVENT_PRESSED`, `false` on `LV_EVENT_RELEASED` (m5's behaviour, on a touch
  button).
- **E-STOP** → on-screen STOP button calls `emergency_stop()`; arc turns red.
  No reliance on an encoder push-button (this BSP does not expose one).

### 5.4 Navigation

```
CONNECTING ──(roster_ready)──► HOME ──► ROSTER ──┐
                                 ├──► RECENT ─────┤(acquire)
                                 └──► SETTINGS    │
                                                  ▼
                          HOME ◄──(back/release)── THROTTLE ◄──┐
                                                     │ FN btn   │
                                                     ▼          │
                                                  FUNCTIONS ────┘(back)
```

## 6. Configuration

Serial console + NVS, on-device read-only.

| NVS key | Default (from `secrets.h`) | Purpose |
|---|---|---|
| `wifi_ssid` | compile-time | Wi-Fi network |
| `wifi_password` | compile-time | Wi-Fi password |
| `jmri_ip` | compile-time | JMRI WiThrottle server IP |
| `jmri_port` | `12090` | JMRI WiThrottle port |
| `udp_log_ip` | empty (disabled) | Optional UDP log host |

- Renames the dial's `withr_ip` / `withr_port` to `jmri_ip` / `jmri_port` for
  clarity and consistency with m5's naming.
- A new `src/secrets.h` (git-ignored, with a committed `secrets.h.example`)
  supplies compile-time defaults; NVS overrides them. Set via the existing
  console: `param set jmri_ip … ; param save`.
- The dial's hardcoded `udp_log_start("192.168.1.160", …)` becomes an optional,
  off-by-default param so a missing log host does not matter.

## 7. Error handling and resilience

Mostly already present in the dial; we preserve and extend it.

- **Wi-Fi / TCP / JMRI drop:** the withr task already retries (5 s TCP, 3 s
  session) and clears `is_connected()`. The UI reacts: if the connection drops
  while on Throttle / Functions, `get_phase()` regresses and the screen shows a
  reconnecting state; on full loss we fall back to the Connecting screen rather
  than act on a dead loco.
- **Acquire before roster ready:** Roster / Recent selection is disabled (greyed)
  until `roster_ready()`. Recents entries whose address is not in the roster are
  still acquirable (JMRI accepts the raw address).
- **Queue full:** command sends stay non-blocking (drop-if-full), safe for speed
  spam from the encoder.
- **Loco released on back:** leaving Throttle calls `release_loco()` so JMRI
  frees the loco. E-stop sets speed 0 and a red arc.
- The dial's `deadlock_monitor_task` (LVGL-lock watchdog) stays available but off
  by default, as in the original.

## 8. Testing

- **Native unit tests** (PlatformIO `native` env, as m5 does), no hardware:
  - `recents`: push / dedup / trim to 5, NVS round-trip (NVS mocked).
  - `resolve_horn_fn`: label scan → correct index / default 2.
  - Roster snapshot sort + address/length parsing (`"S3"` → `{3,'S'}`,
    `"L1234"` → `{1234,'L'}`).
- **On-device manual verification checklist** (documented in README):
  connect → roster loads → acquire → speed arc tracks encoder → direction →
  horn hold → functions toggle → e-stop → back releases → recents persists
  across reboot → reconnect after Wi-Fi drop.
- Logic that touches `WiThrottleProtocol` / LVGL stays behind the `withr::` /
  screen interfaces so the testable parts do not depend on hardware.

## 9. Open questions / risks

- **LVGL 8.4 vs 9:** the dial is on 8.4; m5 code is LVGL 9. Any ported UI snippet
  must be adapted to the 8.4 API (kept minimal by reusing the dial's screens).
- **WiThrottleProtocol fork:** the dial uses the `len0rd/WiThrottleProtocol`
  submodule (ESP-IDF-friendly), already proven to compile and run here.
- **Roster size:** very large JMRI rosters may need lazy list rendering; deferred
  until observed to be a problem.
