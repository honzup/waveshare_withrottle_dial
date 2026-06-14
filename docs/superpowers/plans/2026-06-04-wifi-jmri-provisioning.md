# Wi-Fi & JMRI Provisioning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace compile-time `SECRET_*` Wi-Fi/JMRI hardcoding with on-device config — mDNS auto-discovery of the JMRI server and a SoftAP captive-portal for Wi-Fi credentials — reusing the existing `params::Param`/NVS layer.

**Architecture:** Two independent phases. Phase A adds a `jmri_discovery` component (pure `choose_server` selection logic + a thin `mdns` wrapper) consulted by the WiThrottle connect loop. Phase B adds a `netprov` component (pure `validation` + SoftAP bring-up + a catch-all DNS responder + an `esp_http_server` portal) plus a provisioning screen and boot-flow entry. The custom espota OTA task listens on UDP 3232, so the portal's TCP :80 has no port clash.

**Tech Stack:** ESP-IDF 5.3.2, `espressif/mdns` (managed), `esp_http_server`, `esp_wifi` (APSTA + scan), lwip UDP sockets, LVGL 8.4, existing `params`/`ParamMgr` NVS layer. Native unit tests via Unity (`bash test/native/run.sh`).

**Branch:** `feat/wifi-jmri-provisioning` (already created).

---

## File Structure

**New — `components/jmri_discovery/`**
- `include/jmri_discovery.h` — `struct JmriServer`, `ChooseKind`, `struct ChooseResult`, `discover()` + `choose()` decls (pure header, no IDF includes).
- `choose_server.cpp` — pure selection logic (native-testable).
- `jmri_discovery.cpp` — `discover()`: mdns browse wrapper (IDF-only).
- `CMakeLists.txt`.

**New — `components/netprov/`**
- `include/netprov.h` — `netprov_start/stop/is_active` lifecycle (C linkage).
- `include/netprov_validation.h` — `valid_ipv4`, `valid_port`, `valid_ssid`, `valid_password` (pure header).
- `netprov_validation.cpp` — pure validation (native-testable).
- `netprov.cpp` — SoftAP bring-up + orchestration.
- `captive_dns.cpp` — UDP:53 catch-all responder task.
- `portal_http.cpp` — `esp_http_server` handlers + form HTML + POST parse.
- `CMakeLists.txt`.

**New — `components/ui_screens/screen_provisioning.cpp`** — provisioning instructions + (Phase A) JMRI picker screen.

**Modified**
- `test/native/run.sh` — add include dirs + pure sources for the two new modules.
- `components/withrottle_client/withrottle_client.cpp` — discovery-first connect.
- `components/wifi/wifi.h` / `wifi.cpp` — `wifi_has_credentials()`, `wifi_suspend_sta()`.
- `components/ui_screens/include/screen_mgr.h` — add `Screen::PROVISIONING`.
- `components/ui_screens/screen_mgr.cpp` — register provisioning module.
- `components/ui_screens/screen_connecting.cpp` — boot-flow provisioning entry.
- `components/ui_screens/screen_settings.cpp` — "Reconfigure Wi-Fi" button.
- `components/ui_screens/CMakeLists.txt` — add `screen_provisioning.cpp`, REQUIRES `netprov jmri_discovery`.
- `main/idf_component.yml` — add `espressif/mdns`.

---

# PHASE A — JMRI mDNS discovery

### Task A1: `choose_server` pure selection logic

**Files:**
- Create: `components/jmri_discovery/include/jmri_discovery.h`
- Create: `components/jmri_discovery/choose_server.cpp`
- Create: `test/test_choose_server/test_choose_server.cpp`
- Modify: `test/native/run.sh`

- [ ] **Step 1: Write the header**

`components/jmri_discovery/include/jmri_discovery.h`:
```cpp
#pragma once
// Pure header — MUST NOT include any ESP-IDF headers (compiled on native too).
#include <string>
#include <vector>
#include <cstdint>

namespace jmri_discovery {

struct JmriServer {
    std::string name;   // mDNS instance name (may be empty)
    std::string ip;     // dotted IPv4
    uint16_t    port;   // WiThrottle port
};

// Outcome of reconciling discovered servers against the saved fallback.
enum class ChooseKind {
    None,          // nothing discovered and no usable saved IP — keep waiting
    UseSaved,      // nothing discovered; fall back to the saved IP/port
    UseDiscovered, // exactly one discovered — use it (and the caller remembers it)
    Ambiguous      // several discovered — caller must let the user pick
};

struct ChooseResult {
    ChooseKind kind;
    JmriServer server; // valid for UseSaved and UseDiscovered
};

// Pure: decide which server to dial. `found` is the mDNS result set.
ChooseResult choose(const std::vector<JmriServer>& found,
                    const std::string& saved_ip, uint16_t saved_port);

// IDF-only (defined in jmri_discovery.cpp): browse _withrottle._tcp.
std::vector<JmriServer> discover(uint32_t timeout_ms);

} // namespace jmri_discovery
```

- [ ] **Step 2: Write the failing test**

`test/test_choose_server/test_choose_server.cpp`:
```cpp
#include <unity.h>
#include "jmri_discovery.h"
using namespace jmri_discovery;

void test_empty_with_saved_uses_saved() {
    ChooseResult r = choose({}, "192.168.1.46", 12090);
    TEST_ASSERT_EQUAL_INT((int)ChooseKind::UseSaved, (int)r.kind);
    TEST_ASSERT_EQUAL_STRING("192.168.1.46", r.server.ip.c_str());
    TEST_ASSERT_EQUAL_UINT16(12090, r.server.port);
}
void test_empty_without_saved_is_none() {
    ChooseResult r = choose({}, "", 12090);
    TEST_ASSERT_EQUAL_INT((int)ChooseKind::None, (int)r.kind);
}
void test_single_uses_discovered() {
    ChooseResult r = choose({{"JMRI", "10.0.0.5", 12090}}, "192.168.1.46", 12090);
    TEST_ASSERT_EQUAL_INT((int)ChooseKind::UseDiscovered, (int)r.kind);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", r.server.ip.c_str());
}
void test_multiple_is_ambiguous() {
    ChooseResult r = choose({{"A","10.0.0.5",12090},{"B","10.0.0.6",12090}}, "", 0);
    TEST_ASSERT_EQUAL_INT((int)ChooseKind::Ambiguous, (int)r.kind);
}
void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_with_saved_uses_saved);
    RUN_TEST(test_empty_without_saved_is_none);
    RUN_TEST(test_single_uses_discovered);
    RUN_TEST(test_multiple_is_ambiguous);
    return UNITY_END();
}
```

- [ ] **Step 3: Wire the native runner**

In `test/native/run.sh`, add to the `INCLUDES` array:
```bash
          -Icomponents/jmri_discovery/include
          -Icomponents/netprov/include
```
and add to the `PURE` loop's source list (inside the `for s in ... ; do`):
```bash
         components/jmri_discovery/choose_server.cpp \
         components/netprov/netprov_validation.cpp \
```

- [ ] **Step 4: Run test to verify it fails**

Run: `bash test/native/run.sh choose_server`
Expected: FAIL to compile/link — `undefined reference to jmri_discovery::choose`.

- [ ] **Step 5: Implement `choose_server.cpp`**

`components/jmri_discovery/choose_server.cpp`:
```cpp
#include "jmri_discovery.h"

namespace jmri_discovery {

ChooseResult choose(const std::vector<JmriServer>& found,
                    const std::string& saved_ip, uint16_t saved_port) {
    if (found.empty()) {
        if (saved_ip.empty()) return { ChooseKind::None, {} };
        return { ChooseKind::UseSaved, { "", saved_ip, saved_port } };
    }
    if (found.size() == 1) return { ChooseKind::UseDiscovered, found.front() };
    return { ChooseKind::Ambiguous, {} };
}

} // namespace jmri_discovery
```

- [ ] **Step 6: Run test to verify it passes**

Run: `bash test/native/run.sh choose_server`
Expected: PASS — `4 Tests 0 Failures 0 Ignored OK`.

- [ ] **Step 7: Commit**

```bash
git add components/jmri_discovery/include/jmri_discovery.h \
        components/jmri_discovery/choose_server.cpp \
        test/test_choose_server/test_choose_server.cpp test/native/run.sh
git -c user.name='John' -c user.email='honzup@gmail.com' \
    commit -m "Add pure JMRI server selection logic with native tests"
```

---

### Task A2: mDNS discovery wrapper

**Files:**
- Create: `components/jmri_discovery/jmri_discovery.cpp`
- Create: `components/jmri_discovery/CMakeLists.txt`
- Modify: `main/idf_component.yml`

- [ ] **Step 1: Add the mDNS managed dependency**

`main/idf_component.yml` — add under `dependencies:`:
```yaml
  espressif/mdns:
    version: '^1.2.0'
```

- [ ] **Step 2: Write the component CMakeLists**

`components/jmri_discovery/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "choose_server.cpp" "jmri_discovery.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES mdns esp_netif)
```

- [ ] **Step 3: Implement `discover()`**

`components/jmri_discovery/jmri_discovery.cpp`:
```cpp
#include "jmri_discovery.h"
#include "mdns.h"
#include "esp_netif_ip_addr.h"
#include "esp_log.h"

namespace jmri_discovery {

static const char* TAG = "jmri_disc";
static bool s_inited = false;

std::vector<JmriServer> discover(uint32_t timeout_ms) {
    std::vector<JmriServer> out;
    if (!s_inited) {
        if (mdns_init() != ESP_OK) { ESP_LOGW(TAG, "mdns_init failed"); return out; }
        s_inited = true;
    }
    mdns_result_t* results = nullptr;
    if (mdns_query_ptr("_withrottle", "_tcp", timeout_ms, 8, &results) != ESP_OK || !results)
        return out;
    for (mdns_result_t* r = results; r; r = r->next) {
        JmriServer s;
        s.name = r->instance_name ? r->instance_name : "";
        s.port = r->port;
        if (r->addr && r->addr->addr.type == ESP_IPADDR_TYPE_V4) {
            char buf[16] = {0};
            esp_ip4addr_ntoa(&r->addr->addr.u_addr.ip4, buf, sizeof(buf));
            s.ip = buf;
        }
        if (!s.ip.empty()) out.push_back(s);
    }
    mdns_query_results_free(results);
    ESP_LOGI(TAG, "discovered %d JMRI server(s)", (int)out.size());
    return out;
}

} // namespace jmri_discovery
```

- [ ] **Step 4: Build to verify it compiles and pulls mdns**

Run: `./build.sh build 2>&1 | tail -5`
Expected: build completes; `Project build complete`. (First build downloads `espressif/mdns`.)

- [ ] **Step 5: Commit**

```bash
git add components/jmri_discovery/CMakeLists.txt \
        components/jmri_discovery/jmri_discovery.cpp main/idf_component.yml \
        dependencies.lock
git -c user.name='John' -c user.email='honzup@gmail.com' \
    commit -m "Add mDNS WiThrottle discovery wrapper"
```

---

### Task A3: Discovery-first connect in the WiThrottle client

**Files:**
- Modify: `components/withrottle_client/withrottle_client.cpp` (connect loop, ~lines 299–311; the `params::Param` decls at lines 27–28)
- Modify: `components/withrottle_client/CMakeLists.txt` (add `jmri_discovery` to REQUIRES)

- [ ] **Step 1: Add the REQUIRES**

In `components/withrottle_client/CMakeLists.txt`, add `jmri_discovery` to the `REQUIRES` list (keep existing entries).

- [ ] **Step 2: Include the header**

Near the top of `components/withrottle_client/withrottle_client.cpp` add:
```cpp
#include "jmri_discovery.h"
```

- [ ] **Step 3: Replace the saved-IP connect block**

Replace this existing block:
```cpp
        // 2. Connect TCP socket
        std::string withrottle_ip   = s_jmri_ip.get();
        int         withrottle_port = s_jmri_port.get();
        ESP_LOGI(TAG, "Connecting to %s:%d", withrottle_ip.c_str(), withrottle_port);
        if (!client.connect(withrottle_ip.c_str(), withrottle_port))
        {
            ESP_LOGE(TAG, "TCP connect failed, retrying in 5s");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
```
with:
```cpp
        // 2. Resolve the server: mDNS first, saved IP/port as fallback.
        auto found = jmri_discovery::discover(3000);
        auto res   = jmri_discovery::choose(found, s_jmri_ip.get(),
                                            (uint16_t) s_jmri_port.get());
        std::string withrottle_ip;
        int         withrottle_port = 0;
        switch (res.kind)
        {
            case jmri_discovery::ChooseKind::None:
                ESP_LOGW(TAG, "No JMRI via mDNS and no saved IP; retrying in 5s");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            case jmri_discovery::ChooseKind::Ambiguous:
                // Park until the user picks one from the UI (Task A4). The
                // picker writes jmri_ip/jmri_port, after which choose() returns
                // UseSaved on the next pass.
                withr_set_server_choices(found);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            case jmri_discovery::ChooseKind::UseDiscovered:
                withrottle_ip   = res.server.ip;
                withrottle_port = res.server.port;
                s_jmri_ip.set(withrottle_ip);
                s_jmri_port.set(withrottle_port);
                break;
            case jmri_discovery::ChooseKind::UseSaved:
                withrottle_ip   = res.server.ip;
                withrottle_port = res.server.port;
                break;
        }
        ESP_LOGI(TAG, "Connecting to %s:%d", withrottle_ip.c_str(), withrottle_port);
        if (!client.connect(withrottle_ip.c_str(), withrottle_port))
        {
            ESP_LOGE(TAG, "TCP connect failed, retrying in 5s");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
```

- [ ] **Step 4: Add the picker hand-off stub (filled in A4)**

Add near the other file-scope statics in `withrottle_client.cpp`:
```cpp
// Discovered-server list awaiting a user pick (Ambiguous case). Guarded by the
// fact that only the client task writes it and only the UI task reads it via the
// accessor; a stale read at worst shows the list one cycle late.
static std::vector<jmri_discovery::JmriServer> s_server_choices;
static void withr_set_server_choices(const std::vector<jmri_discovery::JmriServer>& v) {
    s_server_choices = v;
}
```
Add to `components/withrottle_client/withrottle_client.h` (inside `namespace withr`):
```cpp
// Servers awaiting a pick when several JMRI instances were found via mDNS.
std::vector<jmri_discovery::JmriServer> get_server_choices();
```
…and in the `.cpp` (inside `namespace withr`) define:
```cpp
std::vector<jmri_discovery::JmriServer> get_server_choices() { return s_server_choices; }
```
Add `#include "jmri_discovery.h"` and `#include <vector>` to `withrottle_client.h`.

- [ ] **Step 5: Build to verify it compiles**

Run: `./build.sh build 2>&1 | tail -5`
Expected: `Project build complete`.

- [ ] **Step 6: Flash and verify on hardware**

```bash
fuser -k /dev/ttyACM0 2>/dev/null; sleep 1; ./build.sh -p /dev/ttyACM0 flash 2>&1 | tail -4
```
Expected: with JMRI advertising WiThrottle, the log shows `discovered 1 JMRI server(s)` and connects without a saved IP. On an mDNS-blocked network it logs 0 discovered and uses the saved IP.

- [ ] **Step 7: Commit**

```bash
git add components/withrottle_client/withrottle_client.cpp \
        components/withrottle_client/withrottle_client.h \
        components/withrottle_client/CMakeLists.txt
git -c user.name='John' -c user.email='honzup@gmail.com' \
    commit -m "Resolve JMRI via mDNS first, falling back to saved IP"
```

---

### Task A4: Multiple-server picker screen

**Files:**
- Create: `components/ui_screens/screen_provisioning.cpp` (this file also hosts the SoftAP instructions added in Task B6; here it gets the JMRI picker portion)
- Modify: `components/ui_screens/include/screen_mgr.h`
- Modify: `components/ui_screens/screen_mgr.cpp`
- Modify: `components/ui_screens/CMakeLists.txt`
- Modify: `components/ui_screens/screen_connecting.cpp`

> Note: To keep this task self-contained, it introduces `screen_provisioning.cpp` with ONLY the JMRI picker. Task B6 extends the SAME file with the SoftAP instruction view. If Phase B is built first, merge accordingly.

- [ ] **Step 1: Add the screen enum value**

In `components/ui_screens/include/screen_mgr.h` change the enum to:
```cpp
enum class Screen { SPLASH, CONNECTING, HOME, ROSTER, RECENT, THROTTLE, FUNCTIONS, SETTINGS, PROVISIONING };
```

- [ ] **Step 2: Register the module**

In `components/ui_screens/screen_mgr.cpp`:
- add after the other externs: `extern ScreenModule g_screen_provisioning;`
- add to `module_for`'s switch: `case Screen::PROVISIONING: return &g_screen_provisioning;`

- [ ] **Step 3: Implement the picker screen**

`components/ui_screens/screen_provisioning.cpp`:
```cpp
#include "screen_mgr.h"
#include "withrottle_client.h"
#include "jmri_discovery.h"
#include "lvgl.h"
#include <string>
#include <vector>

static lv_obj_t* s_scr   = nullptr;
static lv_obj_t* s_list  = nullptr;
static std::vector<jmri_discovery::JmriServer> s_shown; // currently rendered set

static void pick_clicked(lv_event_t* e) {
    intptr_t idx = (intptr_t) lv_event_get_user_data(e);
    if (idx >= 0 && idx < (intptr_t) s_shown.size()) {
        withr::set_jmri_server(s_shown[idx].ip, s_shown[idx].port); // writes params
        screen_mgr_show(Screen::CONNECTING);
    }
}

static void build_if_needed() {
    if (s_scr) return;
    s_scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t* title = lv_label_create(s_scr);
    lv_label_set_text(title, "Choose JMRI server");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, 320, 250);
    lv_obj_align(s_list, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_list, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void rebuild_rows() {
    // Reuse-or-create one button per server (never delete — matches list_widget).
    uint32_t existing = lv_obj_get_child_cnt(s_list);
    for (size_t i = 0; i < s_shown.size(); ++i) {
        lv_obj_t* btn;
        lv_obj_t* lbl;
        if (i < existing) {
            btn = lv_obj_get_child(s_list, i);
            lbl = lv_obj_get_child(btn, 0);
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
        } else {
            btn = lv_btn_create(s_list);
            lv_obj_set_size(btn, 280, 50);
            lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2C4A7E), LV_PART_MAIN | LV_STATE_DEFAULT);
            lbl = lv_label_create(btn);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_center(lbl);
            lv_obj_add_event_cb(btn, pick_clicked, LV_EVENT_CLICKED, (void*) (intptr_t) i);
        }
        std::string name = s_shown[i].name.empty() ? s_shown[i].ip : s_shown[i].name;
        lv_label_set_text(lbl, (name + "  (" + s_shown[i].ip + ")").c_str());
    }
    for (uint32_t i = s_shown.size(); i < existing; ++i)
        lv_obj_add_flag(lv_obj_get_child(s_list, i), LV_OBJ_FLAG_HIDDEN);
}

static void enter() { build_if_needed(); lv_scr_load(s_scr); }

static void tick() {
    // Refresh the list from the client task's discovered set.
    std::vector<jmri_discovery::JmriServer> latest = withr::get_server_choices();
    if (latest.size() != s_shown.size()) { s_shown = latest; rebuild_rows(); }
}

ScreenModule g_screen_provisioning{ enter, nullptr, tick, nullptr };
```

- [ ] **Step 4: Add the `set_jmri_server` API**

In `components/withrottle_client/withrottle_client.h` (inside `namespace withr`):
```cpp
// Persist a chosen JMRI server (writes jmri_ip/jmri_port params -> NVS).
void set_jmri_server(const std::string& ip, uint16_t port);
```
In `withrottle_client.cpp` (inside `namespace withr`):
```cpp
void set_jmri_server(const std::string& ip, uint16_t port) {
    s_jmri_ip.set(ip);
    s_jmri_port.set((int) port);
    s_server_choices.clear(); // resolved — leave the picker
}
```

- [ ] **Step 5: Route the connecting screen to the picker**

In `components/ui_screens/screen_connecting.cpp`, add `#include "jmri_discovery.h"` and extend `tick()` so that, while waiting for the server, it shows the picker when choices appear:
```cpp
static void tick() {
    withr::Phase p = withr::get_phase();
    if (s_label) lv_label_set_text(s_label, phase_text(p));
    if (p == withr::Phase::SERVER && !withr::get_server_choices().empty()) {
        screen_mgr_show(Screen::PROVISIONING);
        return;
    }
    if (p == withr::Phase::READY) {
        screen_mgr_show(withr::want_loco() ? Screen::THROTTLE : Screen::HOME);
    }
}
```

- [ ] **Step 6: Add to the build**

In `components/ui_screens/CMakeLists.txt`, add `"screen_provisioning.cpp"` to `SRCS` and add `jmri_discovery` to `REQUIRES`.

- [ ] **Step 7: Build to verify it compiles**

Run: `./build.sh build 2>&1 | tail -5`
Expected: `Project build complete`.

- [ ] **Step 8: Commit**

```bash
git add components/ui_screens/screen_provisioning.cpp \
        components/ui_screens/include/screen_mgr.h \
        components/ui_screens/screen_mgr.cpp \
        components/ui_screens/screen_connecting.cpp \
        components/ui_screens/CMakeLists.txt \
        components/withrottle_client/withrottle_client.h \
        components/withrottle_client/withrottle_client.cpp
git -c user.name='John' -c user.email='honzup@gmail.com' \
    commit -m "Add JMRI server picker for multiple mDNS results"
```

---

# PHASE B — SoftAP captive-portal provisioning

### Task B1: `netprov_validation` pure logic

**Files:**
- Create: `components/netprov/include/netprov_validation.h`
- Create: `components/netprov/netprov_validation.cpp`
- Create: `test/test_netprov_validation/test_netprov_validation.cpp`

(Include dirs + pure source were already added to `test/native/run.sh` in Task A1, Step 3.)

- [ ] **Step 1: Write the header**

`components/netprov/include/netprov_validation.h`:
```cpp
#pragma once
// Pure header — no ESP-IDF includes.
#include <string>
#include <cstdint>

namespace netprov {
bool valid_ipv4(const std::string& s);              // "1.2.3.4", each octet 0..255
bool valid_port(const std::string& s, uint16_t& out); // numeric, 1..65535
bool valid_ssid(const std::string& s);              // length 1..32
bool valid_password(const std::string& s);          // empty (open) OR length 8..63
}
```

- [ ] **Step 2: Write the failing test**

`test/test_netprov_validation/test_netprov_validation.cpp`:
```cpp
#include <unity.h>
#include "netprov_validation.h"
using namespace netprov;

void test_ipv4_accepts_valid() {
    TEST_ASSERT_TRUE(valid_ipv4("192.168.1.46"));
    TEST_ASSERT_TRUE(valid_ipv4("0.0.0.0"));
    TEST_ASSERT_TRUE(valid_ipv4("255.255.255.255"));
}
void test_ipv4_rejects_invalid() {
    TEST_ASSERT_FALSE(valid_ipv4("192.168.1"));
    TEST_ASSERT_FALSE(valid_ipv4("999.1.1.1"));
    TEST_ASSERT_FALSE(valid_ipv4("abc"));
    TEST_ASSERT_FALSE(valid_ipv4(""));
    TEST_ASSERT_FALSE(valid_ipv4("1.2.3.4.5"));
}
void test_port() {
    uint16_t p = 0;
    TEST_ASSERT_TRUE(valid_port("12090", p)); TEST_ASSERT_EQUAL_UINT16(12090, p);
    TEST_ASSERT_TRUE(valid_port("1", p));
    TEST_ASSERT_FALSE(valid_port("0", p));
    TEST_ASSERT_FALSE(valid_port("70000", p));
    TEST_ASSERT_FALSE(valid_port("x", p));
    TEST_ASSERT_FALSE(valid_port("", p));
}
void test_ssid() {
    TEST_ASSERT_TRUE(valid_ssid("MyNet"));
    TEST_ASSERT_FALSE(valid_ssid(""));
    TEST_ASSERT_FALSE(valid_ssid(std::string(33, 'a')));
}
void test_password() {
    TEST_ASSERT_TRUE(valid_password(""));            // open network
    TEST_ASSERT_TRUE(valid_password("password123"));
    TEST_ASSERT_FALSE(valid_password("short"));      // <8 and non-empty
    TEST_ASSERT_FALSE(valid_password(std::string(64, 'a')));
}
void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ipv4_accepts_valid);
    RUN_TEST(test_ipv4_rejects_invalid);
    RUN_TEST(test_port);
    RUN_TEST(test_ssid);
    RUN_TEST(test_password);
    return UNITY_END();
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `bash test/native/run.sh netprov_validation`
Expected: FAIL — `undefined reference to netprov::valid_ipv4`.

- [ ] **Step 4: Implement `netprov_validation.cpp`**

`components/netprov/netprov_validation.cpp`:
```cpp
#include "netprov_validation.h"
#include <cstdlib>

namespace netprov {

bool valid_ipv4(const std::string& s) {
    int octets = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t start = i;
        int val = 0, digits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            val = val * 10 + (s[i] - '0');
            ++digits; ++i;
            if (val > 255) return false;
        }
        if (digits == 0 || i == start) return false;
        ++octets;
        if (i < s.size()) {
            if (s[i] != '.') return false;
            ++i;
            if (i == s.size()) return false; // trailing dot
        }
    }
    return octets == 4;
}

bool valid_port(const std::string& s, uint16_t& out) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    long v = std::strtol(s.c_str(), nullptr, 10);
    if (v < 1 || v > 65535) return false;
    out = (uint16_t) v;
    return true;
}

bool valid_ssid(const std::string& s)     { return !s.empty() && s.size() <= 32; }
bool valid_password(const std::string& s) { return s.empty() || (s.size() >= 8 && s.size() <= 63); }

} // namespace netprov
```

- [ ] **Step 5: Run test to verify it passes**

Run: `bash test/native/run.sh netprov_validation`
Expected: PASS — `5 Tests 0 Failures 0 Ignored OK`.

- [ ] **Step 6: Commit**

```bash
git add components/netprov/include/netprov_validation.h \
        components/netprov/netprov_validation.cpp \
        test/test_netprov_validation/test_netprov_validation.cpp
git -c user.name='John' -c user.email='honzup@gmail.com' \
    commit -m "Add pure provisioning input validation with native tests"
```

---

### Task B2: SoftAP bring-up + component skeleton

**Files:**
- Create: `components/netprov/include/netprov.h`
- Create: `components/netprov/netprov.cpp`
- Create: `components/netprov/CMakeLists.txt`
- Modify: `components/wifi/wifi.h`, `components/wifi/wifi.cpp`

- [ ] **Step 1: Add wifi helpers (declarations)**

In `components/wifi/wifi.h` add:
```cpp
bool wifi_has_credentials();   // true if both wifi_ssid and wifi_password are non-empty
void wifi_suspend_sta(bool on); // when on, the disconnect handler stops auto-reconnecting
```

- [ ] **Step 2: Implement wifi helpers**

In `components/wifi/wifi.cpp`:
- add a file-scope flag near `s_wifiConnected`:
```cpp
static bool s_sta_suspended = false;
```
- guard the reconnect in `wifi_event_disconnected_handler` — change `esp_wifi_connect();` at the end to:
```cpp
    if (!s_sta_suspended) esp_wifi_connect();
```
- add at the end of the file:
```cpp
bool wifi_has_credentials() {
    return s_ssid.get().length() > 0 && s_password.get().length() > 0;
}
void wifi_suspend_sta(bool on) { s_sta_suspended = on; }
```

- [ ] **Step 3: Write the netprov header**

`components/netprov/include/netprov.h`:
```cpp
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Bring up the SoftAP + captive portal (idempotent). AP SSID: "WiThrottle-Setup".
void netprov_start(void);
void netprov_stop(void);
bool netprov_is_active(void);
#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Write the component CMakeLists**

`components/netprov/CMakeLists.txt`:
```cmake
idf_component_register(SRCS "netprov_validation.cpp" "netprov.cpp"
                            "captive_dns.cpp" "portal_http.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_http_server lwip nvs_flash param)
```

- [ ] **Step 5: Implement `netprov.cpp` (AP bring-up + orchestration)**

`components/netprov/netprov.cpp`:
```cpp
#include "netprov.h"
#include "wifi.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include <string.h>

// Defined in captive_dns.cpp / portal_http.cpp:
void captive_dns_start(void);
void captive_dns_stop(void);
void portal_http_start(void);
void portal_http_stop(void);

static const char* TAG = "netprov";
static bool s_active = false;
static esp_netif_t* s_ap_netif = nullptr;

void netprov_start(void) {
    if (s_active) return;
    ESP_LOGI(TAG, "Starting SoftAP provisioning");
    wifi_suspend_sta(true); // stop STA auto-reconnect churn while the AP is up

    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t ap = {};
    strncpy((char*) ap.ap.ssid, "WiThrottle-Setup", sizeof(ap.ap.ssid));
    ap.ap.ssid_len       = strlen("WiThrottle-Setup");
    ap.ap.channel        = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode       = WIFI_AUTH_OPEN; // open network — config form only

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    captive_dns_start();
    portal_http_start();
    s_active = true;
    ESP_LOGI(TAG, "Provisioning AP up: connect to 'WiThrottle-Setup'");
}

void netprov_stop(void) {
    if (!s_active) return;
    portal_http_stop();
    captive_dns_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_suspend_sta(false);
    s_active = false;
}

bool netprov_is_active(void) { return s_active; }
```

- [ ] **Step 6: Build (after Tasks B3/B4 provide the dns/http symbols)**

> This task does not build alone because `netprov.cpp` references `captive_dns_*` and `portal_http_*`. Proceed to B3 and B4, then build at the end of B4.

- [ ] **Step 7: Commit**

```bash
git add components/netprov/include/netprov.h components/netprov/netprov.cpp \
        components/netprov/CMakeLists.txt components/wifi/wifi.h components/wifi/wifi.cpp
git -c user.name='John' -c user.email='honzup@gmail.com' \
    commit -m "Add SoftAP bring-up skeleton and wifi suspend/creds helpers"
```

---

### Task B3: Captive DNS responder

**Files:**
- Create: `components/netprov/captive_dns.cpp`

- [ ] **Step 1: Implement the DNS responder**

`components/netprov/captive_dns.cpp`:
```cpp
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <string.h>

static const char* TAG = "captive_dns";
static TaskHandle_t s_task = nullptr;
static volatile bool s_run = false;

// Answer every A query with 192.168.4.1 (the SoftAP gateway) so the phone's
// captive-portal check resolves to us and pops the config page.
static void dns_task(void*) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "socket failed"); s_task = nullptr; vTaskDelete(nullptr); return; }
    struct sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(53);
    if (bind(sock, (struct sockaddr*) &local, sizeof(local)) < 0) {
        ESP_LOGE(TAG, "bind 53 failed"); close(sock); s_task = nullptr; vTaskDelete(nullptr); return;
    }
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512];
    while (s_run) {
        struct sockaddr_in from = {};
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*) &from, &flen);
        if (n < (int) sizeof(uint16_t) * 6) continue; // too short for a header + question

        // Build a minimal response: copy the query, set QR + AA flags, ANCOUNT=1,
        // and append an A record pointing the queried name (compressed) to .4.1.
        buf[2] |= 0x80;            // QR = response
        buf[3]  = 0x00;            // RA cleared, RCODE 0
        buf[7]  = 0x01;            // ANCOUNT = 1 (QDCOUNT already 1 from the query)

        int len = n;
        uint8_t ans[] = {
            0xC0, 0x0C,            // name: pointer to the question (offset 12)
            0x00, 0x01,            // TYPE A
            0x00, 0x01,            // CLASS IN
            0x00, 0x00, 0x00, 0x3C,// TTL 60s
            0x00, 0x04,            // RDLENGTH 4
            192, 168, 4, 1         // RDATA 192.168.4.1
        };
        if (len + (int) sizeof(ans) <= (int) sizeof(buf)) {
            memcpy(buf + len, ans, sizeof(ans));
            len += sizeof(ans);
            sendto(sock, buf, len, 0, (struct sockaddr*) &from, flen);
        }
    }
    close(sock);
    s_task = nullptr;
    vTaskDelete(nullptr);
}

void captive_dns_start(void) {
    if (s_task) return;
    s_run = true;
    xTaskCreate(dns_task, "captive_dns", 4 * 1024, nullptr, 4, &s_task);
}
void captive_dns_stop(void) { s_run = false; }
```

- [ ] **Step 2: Commit**

```bash
git add components/netprov/captive_dns.cpp
git -c user.name='John' -c user.email='honzup@gmail.com' \
    commit -m "Add captive-portal DNS responder"
```

---

### Task B4: Captive-portal HTTP server

**Files:**
- Create: `components/netprov/portal_http.cpp`

- [ ] **Step 1: Implement the portal**

`components/netprov/portal_http.cpp`:
```cpp
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "ParamMgr.h"
#include "netprov_validation.h"
#include <string>
#include <vector>

static const char* TAG = "portal_http";
static httpd_handle_t s_server = nullptr;

static const char FORM[] =
"<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>WiThrottle Setup</title><style>body{font-family:sans-serif;margin:1.5em;max-width:30em}"
"label{display:block;margin:.6em 0 .2em}input,select{width:100%;padding:.5em;box-sizing:border-box}"
"button{margin-top:1em;padding:.7em;width:100%;font-size:1.1em}</style></head><body>"
"<h2>WiThrottle Setup</h2><form method=POST action=/save>"
"<label>Wi-Fi network</label><select name=ssid id=ssid></select>"
"<label>Password</label><input name=password type=password>"
"<label>JMRI IP (optional — auto-discovered if blank)</label><input name=jmri_ip placeholder=192.168.1.46>"
"<label>JMRI port</label><input name=jmri_port value=12090>"
"<button type=submit>Save &amp; restart</button></form>"
"<script>fetch('/scan').then(r=>r.json()).then(a=>{let s=document.getElementById('ssid');"
"a.forEach(n=>{let o=document.createElement('option');o.text=n;o.value=n;s.add(o)})});</script>"
"</body></html>";

// --- tiny form helpers (kept local; logic-light) ---
static std::string url_decode(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '+') out += ' ';
        else if (in[i] == '%' && i + 2 < in.size()) {
            auto hex = [](char c)->int{ if(c>='0'&&c<='9')return c-'0';
                if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return 0; };
            out += (char)(hex(in[i+1]) * 16 + hex(in[i+2])); i += 2;
        } else out += in[i];
    }
    return out;
}
static std::string field(const std::string& body, const std::string& key) {
    std::string k = key + "=";
    size_t p = body.find(k);
    while (p != std::string::npos && p != 0 && body[p-1] != '&') p = body.find(k, p + 1);
    if (p == std::string::npos) return "";
    p += k.size();
    size_t e = body.find('&', p);
    return url_decode(body.substr(p, e == std::string::npos ? std::string::npos : e - p));
}

static esp_err_t get_root(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, FORM, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t get_scan(httpd_req_t* req) {
    uint16_t n = 0;
    esp_wifi_scan_start(nullptr, true);
    esp_wifi_scan_get_ap_num(&n);
    if (n > 20) n = 20;
    std::vector<wifi_ap_record_t> recs(n);
    esp_wifi_scan_get_ap_records(&n, recs.data());
    std::string json = "[";
    for (uint16_t i = 0; i < n; ++i) {
        if (i) json += ",";
        json += "\""; json += (char*) recs[i].ssid; json += "\"";
    }
    json += "]";
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
}

static esp_err_t post_save(httpd_req_t* req) {
    std::string body;
    body.resize(req->content_len);
    int got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, &body[got], req->content_len - got);
        if (r <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
        got += r;
    }
    std::string ssid = field(body, "ssid");
    std::string pass = field(body, "password");
    std::string ip   = field(body, "jmri_ip");
    std::string port = field(body, "jmri_port");

    using namespace netprov;
    uint16_t p = 0;
    bool ok = valid_ssid(ssid) && valid_password(pass)
              && (ip.empty()   || valid_ipv4(ip))
              && (port.empty() || valid_port(port, p));
    if (!ok) {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req,
            "<html><body><h3>Invalid input</h3><a href=/>Back</a></body></html>",
            HTTPD_RESP_USE_STRLEN);
    }

    auto& pm = params::ParamMgr::getInstance();
    pm.setParam("wifi_ssid", ssid);
    pm.setParam("wifi_password", pass);
    if (!ip.empty())   pm.setParam("jmri_ip", ip);
    if (!port.empty()) pm.setParam("jmri_port", port);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req,
        "<html><body><h3>Saved. Restarting…</h3></body></html>", HTTPD_RESP_USE_STRLEN);
    ESP_LOGI(TAG, "Provisioned ssid='%s'; restarting", ssid.c_str());
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
    return ESP_OK;
}

// Redirect any other path to the form (OS captive-portal probes).
static esp_err_t get_redirect(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, "", 0);
}

void portal_http_start(void) {
    if (s_server) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port      = 80;
    cfg.uri_match_fn     = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 8;
    if (httpd_start(&s_server, &cfg) != ESP_OK) { ESP_LOGE(TAG, "httpd_start failed"); return; }
    httpd_uri_t u_root   = { "/",         HTTP_GET,  get_root,     nullptr };
    httpd_uri_t u_scan   = { "/scan",     HTTP_GET,  get_scan,     nullptr };
    httpd_uri_t u_save   = { "/save",     HTTP_POST, post_save,    nullptr };
    httpd_uri_t u_any    = { "/*",        HTTP_GET,  get_redirect, nullptr };
    httpd_register_uri_handler(s_server, &u_root);
    httpd_register_uri_handler(s_server, &u_scan);
    httpd_register_uri_handler(s_server, &u_save);
    httpd_register_uri_handler(s_server, &u_any);
}
void portal_http_stop(void) {
    if (s_server) { httpd_stop(s_server); s_server = nullptr; }
}
```

- [ ] **Step 2: Build to verify the whole netprov component compiles**

Run: `./build.sh build 2>&1 | tail -5`
Expected: `Project build complete`. (netprov is not yet referenced by the app, so it links but is dormant — that is fine; Task B5 wires it in.)

- [ ] **Step 3: Commit**

```bash
git add components/netprov/portal_http.cpp
git -c user.name='John' -c user.email='honzup@gmail.com' \
    commit -m "Add captive-portal HTTP config server"
```

---

### Task B5: Boot-flow provisioning entry

**Files:**
- Modify: `components/ui_screens/screen_connecting.cpp`
- Modify: `components/ui_screens/CMakeLists.txt` (add `netprov` to REQUIRES)

- [ ] **Step 1: Add includes + a timeout to the connecting screen**

In `components/ui_screens/screen_connecting.cpp` add includes:
```cpp
#include "netprov.h"
#include "wifi.h"
#include "esp_timer.h"
```
Add a file-scope timestamp and record it in `enter()`:
```cpp
static uint64_t s_enter_us = 0;
```
At the end of the existing `enter()` (after `lv_scr_load(s_scr);`):
```cpp
    s_enter_us = esp_timer_get_time();
```

- [ ] **Step 2: Add the provisioning decision to `tick()`**

Extend `tick()` (building on the A4 version) so the final form is:
```cpp
static void tick() {
    withr::Phase p = withr::get_phase();
    if (s_label) lv_label_set_text(s_label, phase_text(p));

    // Enter provisioning if there are no stored creds, or Wi-Fi has failed to
    // associate for 30s. (Once associated, p advances past WIFI and we skip this.)
    if (!netprov_is_active() && p == withr::Phase::WIFI) {
        bool no_creds = !wifi_has_credentials();
        bool timed_out = (esp_timer_get_time() - s_enter_us) > 30ULL * 1000 * 1000;
        if (no_creds || timed_out) {
            netprov_start();
            screen_mgr_show(Screen::PROVISIONING);
            return;
        }
    }

    if (p == withr::Phase::SERVER && !withr::get_server_choices().empty()) {
        screen_mgr_show(Screen::PROVISIONING);
        return;
    }
    if (p == withr::Phase::READY) {
        screen_mgr_show(withr::want_loco() ? Screen::THROTTLE : Screen::HOME);
    }
}
```

- [ ] **Step 3: Add netprov to ui_screens REQUIRES**

In `components/ui_screens/CMakeLists.txt` add `netprov` to the `REQUIRES` list.

- [ ] **Step 4: Build to verify it compiles**

Run: `./build.sh build 2>&1 | tail -5`
Expected: `Project build complete`.

- [ ] **Step 5: Flash and verify on hardware**

```bash
fuser -k /dev/ttyACM0 2>/dev/null; sleep 1; ./build.sh -p /dev/ttyACM0 flash 2>&1 | tail -4
```
Manual verification (temporarily set blank creds via serial first: `param set wifi_ssid ""` then reset, OR test on a network the device can't join): the `WiThrottle-Setup` AP appears; joining it from a phone auto-opens the form; submitting valid Wi-Fi details reboots the device and it connects.

- [ ] **Step 6: Commit**

```bash
git add components/ui_screens/screen_connecting.cpp components/ui_screens/CMakeLists.txt
git -c user.name='John' -c user.email='honzup@gmail.com' \
    commit -m "Enter SoftAP provisioning on missing creds or Wi-Fi timeout"
```

---

### Task B6: Provisioning instructions view + Settings trigger

**Files:**
- Modify: `components/ui_screens/screen_provisioning.cpp` (add an instructions view shown when SoftAP is active)
- Modify: `components/ui_screens/screen_settings.cpp` (add "Reconfigure Wi-Fi" button)

- [ ] **Step 1: Show instructions when the AP is up**

In `components/ui_screens/screen_provisioning.cpp` add `#include "netprov.h"` and a second label built in `build_if_needed()` (after the title):
```cpp
    lv_obj_t* info = lv_label_create(s_scr);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info, 300);
    lv_label_set_text(info,
        "Join Wi-Fi 'WiThrottle-Setup' on your phone, then follow the page that opens.");
    lv_obj_set_style_text_color(info, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(info, LV_OBJ_FLAG_HIDDEN);
    s_info = info; // add `static lv_obj_t* s_info = nullptr;` near s_list
```
In `tick()`, choose which view to show:
```cpp
    bool ap = netprov_is_active();
    if (s_info) { if (ap) lv_obj_clear_flag(s_info, LV_OBJ_FLAG_HIDDEN);
                  else    lv_obj_add_flag(s_info, LV_OBJ_FLAG_HIDDEN); }
    if (s_list) { if (ap) lv_obj_add_flag(s_list, LV_OBJ_FLAG_HIDDEN);
                  else    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN); }
```
(The picker list and the SoftAP instructions are mutually exclusive: instructions while the AP is up, the JMRI picker otherwise.)

- [ ] **Step 2: Add the Settings button**

In `components/ui_screens/screen_settings.cpp` add `#include "netprov.h"` and, inside the `if (!s_wired)` block, create a button:
```cpp
        lv_obj_t* reconf = lv_btn_create(ui_Settings_Screen);
        lv_obj_set_size(reconf, 240, 56);
        lv_obj_align(reconf, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_set_style_radius(reconf, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(reconf, lv_color_hex(0x2C4A7E), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t* rl = lv_label_create(reconf);
        lv_label_set_text(rl, "Reconfigure Wi-Fi");
        lv_obj_set_style_text_color(rl, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(rl);
        lv_obj_add_event_cb(reconf, [](lv_event_t*){
            netprov_start();
            screen_mgr_show(Screen::PROVISIONING);
        }, LV_EVENT_CLICKED, nullptr);
```

- [ ] **Step 3: Build to verify it compiles**

Run: `./build.sh build 2>&1 | tail -5`
Expected: `Project build complete`.

- [ ] **Step 4: Flash and verify on hardware**

```bash
fuser -k /dev/ttyACM0 2>/dev/null; sleep 1; ./build.sh -p /dev/ttyACM0 flash 2>&1 | tail -4
```
Manual verification: from a connected device, Settings → "Reconfigure Wi-Fi" brings up the instructions screen and the `WiThrottle-Setup` AP; the picker view appears instead when several JMRI servers are discovered.

- [ ] **Step 5: Commit**

```bash
git add components/ui_screens/screen_provisioning.cpp components/ui_screens/screen_settings.cpp
git -c user.name='John' -c user.email='honzup@gmail.com' \
    commit -m "Add provisioning instructions view and Settings reconfigure button"
```

---

## Final verification

- [ ] **Run all native tests**

Run: `bash test/native/run.sh`
Expected: every `test_*` suite passes, including `test_choose_server` and `test_netprov_validation`.

- [ ] **End-to-end on hardware**
  - First boot with no creds → AP appears, captive page auto-opens, submit → reconnects.
  - Wrong password → device reboots, fails 30s, returns to provisioning.
  - JMRI found via mDNS with no saved IP; mDNS-blocked → saved-IP fallback.
  - "Reconfigure Wi-Fi" mid-session → provisioning.
  - Regression: valid stored creds → connects exactly as before, no provisioning detour.
