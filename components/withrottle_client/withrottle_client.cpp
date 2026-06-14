/*
 * withrottle_client.cpp — application glue between the UI and the
 * WiThrottleProtocol library: a background task owns the TCP/WiThrottle session
 * while the UI drives it through a command queue and reads cached state back.
 *
 * Independently authored for waveshare_withrottle_dial. The WiThrottleProtocol
 * library itself (components/withrottle) is third-party and carries its own
 * licence; this file only consumes its public API.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John (honzup)
 */
#include "withrottle_client.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <map>
#include <new>
#include <string>
#include <vector>

#include "SocketStream.h"
#include "WiThrottleProtocol.h"
#include "horn_resolver.h"
#include "jmri_discovery.h"
#include "loco_ref.h"
#include "wifi.h"

#include "Param.h"

// secrets.h holds compile-time Wi-Fi/JMRI defaults and is git-ignored. When it
// is absent (e.g. a fresh public clone) we fall back to empty/standard values so
// the firmware still builds and provisions on-device.
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif
#ifndef SECRET_JMRI_IP
#  define SECRET_JMRI_IP ""
#endif
#ifndef SECRET_JMRI_PORT
#  define SECRET_JMRI_PORT 12090
#endif

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr const char *TAG = "withr_client";

constexpr const char *kDeviceName = "withrottle_dial";
constexpr uint8_t      kMaxSpeed  = 126;  // WiThrottle speed step ceiling

// The background task both runs mDNS discovery and parses the full roster in
// the same context; 4 KB overflowed its stack on roster load, so give it 8 KB.
constexpr uint32_t    kTaskStack = 8 * 1024;
constexpr UBaseType_t kTaskPrio  = 4;  // under LVGL (5), over the UI updater (2-3)

// Persisted server endpoint (NVS keys must stay stable across releases).
params::Param<std::string> s_jmri_ip{"jmri_ip", std::string(SECRET_JMRI_IP)};
params::Param<int>         s_jmri_port{"jmri_port", SECRET_JMRI_PORT};

// WiThrottleProtocol carries a ~32 KB receive buffer. Leaving it in internal
// .bss starved the LCD's DMA pool and stalled LVGL, so it lives in PSRAM and is
// constructed once in client_start() before the task touches it.
WiThrottleProtocol *s_proto = nullptr;
inline WiThrottleProtocol &proto() { return *s_proto; }

// ---- Commands posted from the UI task -------------------------------------

struct Command {
    enum Kind { Speed, Direction, EStop, Function, Acquire, Release } kind;
    int  arg        = 0;      // speed step, direction (1=fwd/0=rev), or function index
    bool on         = false;  // Function: desired state
    int  address    = 0;      // Acquire: DCC address
    char length     = 'L';    // Acquire: 'S' or 'L'
    char throttle   = DEFAULT_MULTITHROTTLE;
};

QueueHandle_t s_queue = nullptr;

// ---- Shared session state (UI task <-> client task) -----------------------

// "connected and driving a loco" — true while acquired, cleared on a loco
// release AND on a TCP disconnect. Gates the speed/direction/function reads.
std::atomic<bool> s_connected{false};
// "a loco is acquired right now" — set/cleared by the server's add/remove
// callbacks only; deliberately NOT cleared on disconnect, so the next acquire
// knows to release the stale loco first.
std::atomic<bool> s_loco_active{false};
std::atomic<bool> s_session_up{false};    // WiThrottle/TCP session established
std::atomic<bool> s_roster_ready{false};  // at least one roster entry received
std::atomic<bool> s_loco_wanted{false};   // a loco is selected (survives reconnects)
std::atomic<int>  s_horn_fn{2};           // resolved horn index for the active loco

// The currently-selected loco, remembered so the task can re-acquire it after a
// reconnect. Only the client task writes these.
int  s_sel_address = 0;
char s_sel_length  = 'L';

// ---- Small scope-guard for the roster mutex -------------------------------

SemaphoreHandle_t s_roster_lock = nullptr;

class Guard {
public:
    explicit Guard(SemaphoreHandle_t s) : m_s(s)
    {
        if (m_s) xSemaphoreTake(m_s, portMAX_DELAY);
    }
    ~Guard()
    {
        if (m_s) xSemaphoreGive(m_s);
    }

private:
    SemaphoreHandle_t m_s;
};

// ---- Pending server picks (set when discovery is ambiguous) ---------------

SemaphoreHandle_t                              s_choices_lock = nullptr;
std::vector<jmri_discovery::JmriServer>        s_choices;

void publish_choices(const std::vector<jmri_discovery::JmriServer> &v)
{
    Guard g(s_choices_lock);
    s_choices = v;
}

// ---- Speed <-> percent ----------------------------------------------------

inline uint8_t percent_to_step(uint8_t percent) { return (percent * kMaxSpeed) / 100; }
inline uint8_t step_to_percent(uint8_t step) { return (step * 100) / kMaxSpeed; }

// ---- Delegate: caches everything the server reports -----------------------

class ClientDelegate : public WiThrottleProtocolDelegate {
public:
    struct FnSlot {
        std::string name;
        bool        on = false;
    };

    struct LocoEntry {
        static constexpr char kUnbound = '\0';
        std::string                          name;
        char                                 length      = 'L';   // 'S' or 'L'
        char                                 throttle_id = kUnbound;
        std::array<FnSlot, MAX_FUNCTIONS>    fns{};
    };

    // Caller must hold s_roster_lock. Returns the entry bound to `throttle`, or
    // nullptr if none is currently acquired there.
    LocoEntry *onThrottle(char throttle)
    {
        for (auto &kv : entries) {
            if (kv.second.throttle_id == throttle) return &kv.second;
        }
        return nullptr;
    }

    void receivedVersion(String version) override
    {
        ESP_LOGI(TAG, "server version %s", version.c_str());
    }

    void receivedTrackPower(TrackPower state) override
    {
        ESP_LOGI(TAG, "track power %d", static_cast<int>(state));
    }

    void addressAdded(String address, String entry) override
    {
        addressAddedMultiThrottle(DEFAULT_MULTITHROTTLE, address, entry);
    }

    void addressAddedMultiThrottle(char throttle, String address, String /*entry*/) override
    {
        if (address.length() < 2) return;
        const char len = address[0];
        const int  num = std::stoi(address.substring(1).c_str());
        ESP_LOGI(TAG, "loco acquired %s", address.c_str());
        {
            Guard g(s_roster_lock);
            // One loco per throttle. The server never echoes a client-side
            // release, so detach whatever was on this throttle before binding
            // the new loco — otherwise the name lookups keep returning the old
            // one and the throttle never appears to change.
            if (LocoEntry *prev = onThrottle(throttle)) {
                prev->throttle_id = LocoEntry::kUnbound;
            }
            for (auto &kv : entries) {
                if (kv.first == num && kv.second.length == len) {
                    kv.second.throttle_id = throttle;
                    break;
                }
            }
        }
        s_sel_address = num;
        s_sel_length  = len;
        s_loco_active = true;
        s_connected   = true;
    }

    void addressRemoved(String address, String command) override
    {
        addressRemovedMultiThrottle(DEFAULT_MULTITHROTTLE, address, command);
    }

    void addressRemovedMultiThrottle(char throttle, String address, String /*command*/) override
    {
        if (address.length() >= 2) {
            const char len = address[0];
            const int  num = std::stoi(address.substring(1).c_str());
            Guard      g(s_roster_lock);
            for (auto &kv : entries) {
                if (kv.first == num && kv.second.length == len) {
                    kv.second.throttle_id = LocoEntry::kUnbound;
                    break;
                }
            }
        }
        ESP_LOGI(TAG, "loco released %s", address.c_str());
        s_loco_active = false;
        s_connected   = false;
    }

    void receivedDirection(Direction dir) override { direction = dir; }

    void receivedRosterEntry(int index, String name, int address, char length) override
    {
        {
            Guard g(s_roster_lock);
            LocoEntry &e = entries[address];
            e.name       = name.c_str();
            e.length     = length;
            e.fns[0].name = "Light";  // FN0 is conventionally the headlight
        }
        s_roster_ready = true;
        ESP_LOGI(TAG, "roster[%d] '%s' (%c%d)", index, name.c_str(), length, address);
    }

    void receivedRosterFunctionList(String functions[MAX_FUNCTIONS]) override
    {
        receivedRosterFunctionListMultiThrottle(DEFAULT_MULTITHROTTLE, functions);
    }

    void receivedRosterFunctionListMultiThrottle(char throttle, String fns[MAX_FUNCTIONS]) override
    {
        Guard      g(s_roster_lock);
        LocoEntry *e = onThrottle(throttle);
        if (e == nullptr) return;

        for (size_t i = 1; i < MAX_FUNCTIONS; ++i) {
            e->fns[i].name = fns[i].c_str();
        }
        // Recompute which function index is the horn for this loco.
        std::vector<std::string> names;
        names.reserve(MAX_FUNCTIONS);
        for (const auto &slot : e->fns) names.push_back(slot.name);
        s_horn_fn = resolve_horn_fn(names);
    }

    void receivedFunctionState(uint8_t func, bool state) override
    {
        receivedFunctionStateMultiThrottle(DEFAULT_MULTITHROTTLE, func, state);
    }

    void receivedFunctionStateMultiThrottle(char throttle, uint8_t func, bool state) override
    {
        if (func >= MAX_FUNCTIONS) return;
        Guard      g(s_roster_lock);
        LocoEntry *e = onThrottle(throttle);
        if (e) e->fns[func].on = state;
    }

    Direction                 direction = Direction::Forward;
    std::map<int, LocoEntry>  entries;  // key = DCC address
};

ClientDelegate s_delegate;

// ---- Resolve + connect to a JMRI server -----------------------------------
// Returns true with ip/port filled when a server is ready to connect to; false
// means "keep waiting" (the task should loop again after its own delay).

bool resolve_server(std::string &ip, int &port)
{
    auto found  = jmri_discovery::discover(3000);
    auto choice = jmri_discovery::choose(found, s_jmri_ip.get(),
                                         static_cast<uint16_t>(s_jmri_port.get()));
    switch (choice.kind) {
        case jmri_discovery::ChooseKind::None:
            ESP_LOGW(TAG, "no JMRI discovered and none saved; waiting");
            vTaskDelay(pdMS_TO_TICKS(5000));
            return false;

        case jmri_discovery::ChooseKind::Ambiguous:
            // Several servers found — hand them to the UI picker and wait. Once
            // the user saves one, choose() resolves it to UseDiscovered.
            publish_choices(found);
            vTaskDelay(pdMS_TO_TICKS(1000));
            return false;

        case jmri_discovery::ChooseKind::UseDiscovered:
            ip   = choice.server.ip;
            port = choice.server.port;
            if (ip != s_jmri_ip.get()) s_jmri_ip.set(ip);
            if (port != s_jmri_port.get()) s_jmri_port.set(port);
            return true;

        case jmri_discovery::ChooseKind::UseSaved:
            ip   = choice.server.ip;
            port = choice.server.port;
            return true;
    }
    return false;
}

void apply_command(const Command &cmd)
{
    switch (cmd.kind) {
        case Command::Speed:
            proto().setSpeed(cmd.throttle, cmd.arg);
            break;
        case Command::Direction:
            proto().setDirection(cmd.throttle, cmd.arg ? Forward : Reverse);
            break;
        case Command::EStop:
            ESP_LOGI(TAG, "emergency stop");
            proto().emergencyStop(cmd.throttle);
            break;
        case Command::Function:
            proto().setFunction(cmd.throttle, cmd.arg, cmd.on);
            break;
        case Command::Acquire: {
            const std::string token = loco_format_address(cmd.address, cmd.length);
            if (s_loco_active) proto().releaseLocomotive(DEFAULT_MULTITHROTTLE);
            proto().addLocomotive(DEFAULT_MULTITHROTTLE, String(token.c_str()));
            s_sel_address = cmd.address;
            s_sel_length  = cmd.length;
            s_loco_wanted = true;
            break;
        }
        case Command::Release:
            if (s_loco_active) proto().releaseLocomotive(DEFAULT_MULTITHROTTLE);
            s_loco_wanted = false;
            break;
    }
}

void client_task(void *)
{
    SocketStream socket;
    proto().setDelegate(&s_delegate);

    for (;;) {
        while (!is_wifi_connected()) {
            ESP_LOGI(TAG, "waiting for Wi-Fi");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        std::string ip;
        int         port = 0;
        if (!resolve_server(ip, port)) {
            continue;  // resolve_server already delayed
        }

        ESP_LOGI(TAG, "connecting to %s:%d", ip.c_str(), port);
        if (!socket.connect(ip.c_str(), port)) {
            ESP_LOGE(TAG, "TCP connect failed; retrying");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        proto().connect(&socket, 100);
        s_session_up = true;
        proto().setDeviceName(kDeviceName);

        // Re-bind the previously selected loco after a reconnect so a brief
        // outage drops the user straight back onto the same engine.
        if (s_loco_wanted) {
            const std::string token = loco_format_address(s_sel_address, s_sel_length);
            proto().addLocomotive(DEFAULT_MULTITHROTTLE, String(token.c_str()));
            ESP_LOGI(TAG, "re-acquiring %s after reconnect", token.c_str());
        }
        ESP_LOGI(TAG, "WiThrottle session up");

        while (socket.connected()) {
            Command cmd;
            while (xQueueReceive(s_queue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
                apply_command(cmd);
            }
            proto().check();  // pump RX + heartbeat
        }

        ESP_LOGW(TAG, "disconnected; retrying in 3s");
        s_connected  = false;  // leave s_loco_active set (matches reconnect logic)
        s_session_up = false;
        socket.disconnect();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// Post a command to the client task; dropped silently if the queue is full.
void post(const Command &cmd)
{
    if (s_queue) xQueueSend(s_queue, &cmd, 0);
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

namespace withr {

void client_start(void)
{
    void *mem = heap_caps_malloc(sizeof(WiThrottleProtocol), MALLOC_CAP_SPIRAM);
    if (mem == nullptr) {
        ESP_LOGE(TAG, "PSRAM allocation for WiThrottleProtocol failed");
        return;
    }
    s_proto = new (mem) WiThrottleProtocol();

    s_roster_lock  = xSemaphoreCreateMutex();
    s_choices_lock = xSemaphoreCreateMutex();
    s_queue        = xQueueCreate(8, sizeof(Command));

    if (xTaskCreate(client_task, "withr", kTaskStack, nullptr, kTaskPrio, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "could not start client task");
    } else {
        ESP_LOGI(TAG, "client task started");
    }
}

std::optional<uint8_t> get_speed()
{
    if (!is_connected()) return std::nullopt;
    const int step = proto().getSpeed(DEFAULT_MULTITHROTTLE);
    if (step < 0 || step > kMaxSpeed) return std::nullopt;
    return step_to_percent(step);
}

std::optional<Direction> get_direction()
{
    if (!is_connected()) return std::nullopt;
    return s_delegate.direction;
}

void set_speed(uint8_t percent)
{
    if (percent > 100) percent = 100;
    Command cmd;
    cmd.kind = Command::Speed;
    cmd.arg  = percent_to_step(percent);
    post(cmd);
}

void emergency_stop(void)
{
    Command cmd;
    cmd.kind = Command::EStop;
    post(cmd);
}

void set_direction(Direction dir)
{
    Command cmd;
    cmd.kind = Command::Direction;
    cmd.arg  = (dir == Direction::Forward) ? 1 : 0;
    post(cmd);
}

bool is_connected(void) { return s_connected.load(); }

std::optional<bool> get_function_state(uint8_t func)
{
    if (!is_connected() || func >= MAX_FUNCTIONS) return std::nullopt;
    Guard g(s_roster_lock);
    if (auto *e = s_delegate.onThrottle(DEFAULT_MULTITHROTTLE)) {
        return e->fns[func].on;
    }
    return std::nullopt;
}

std::optional<std::string> get_function_name(uint8_t func)
{
    if (func >= MAX_FUNCTIONS) return std::nullopt;
    Guard g(s_roster_lock);
    if (auto *e = s_delegate.onThrottle(DEFAULT_MULTITHROTTLE)) {
        return e->fns[func].name;
    }
    return std::nullopt;
}

void set_function(uint8_t func, bool state)
{
    if (func >= MAX_FUNCTIONS) return;
    Command cmd;
    cmd.kind = Command::Function;
    cmd.arg  = func;
    cmd.on   = state;
    post(cmd);
}

std::optional<std::string> get_loco_name()
{
    Guard g(s_roster_lock);
    if (auto *e = s_delegate.onThrottle(DEFAULT_MULTITHROTTLE)) {
        return e->name;
    }
    return std::nullopt;
}

std::string get_server_url()
{
    return s_jmri_ip.get() + ":" + std::to_string(s_jmri_port.get());
}

std::vector<jmri_discovery::JmriServer> get_server_choices()
{
    Guard g(s_choices_lock);
    return s_choices;
}

void set_jmri_server(const std::string &ip, uint16_t port)
{
    if (ip != s_jmri_ip.get()) s_jmri_ip.set(ip);
    if (static_cast<int>(port) != s_jmri_port.get()) s_jmri_port.set(static_cast<int>(port));
    publish_choices({});  // resolved — dismiss the picker
}

Phase get_phase()
{
    if (!is_wifi_connected()) return Phase::WIFI;
    if (!s_session_up.load()) return Phase::SERVER;
    if (!s_roster_ready.load()) return Phase::ROSTER;
    return Phase::READY;
}

bool roster_ready() { return s_roster_ready.load(); }

std::vector<LocoRef> get_roster()
{
    std::vector<LocoRef> out;
    {
        Guard g(s_roster_lock);
        out.reserve(s_delegate.entries.size());
        for (const auto &kv : s_delegate.entries) {
            out.push_back(LocoRef{kv.first, kv.second.length, kv.second.name});
        }
    }
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    };
    std::sort(out.begin(), out.end(),
              [&](const LocoRef &a, const LocoRef &b) { return lower(a.name) < lower(b.name); });
    return out;
}

void acquire_loco(int address, char length)
{
    Command cmd;
    cmd.kind    = Command::Acquire;
    cmd.address = address;
    cmd.length  = length;
    post(cmd);
}

void release_loco()
{
    Command cmd;
    cmd.kind = Command::Release;
    post(cmd);
}

bool has_loco() { return s_loco_active.load(); }

bool want_loco() { return s_loco_wanted.load(); }

int get_horn_fn() { return s_horn_fn.load(); }

void horn(bool on) { set_function(get_horn_fn(), on); }

}  // namespace withr
