# Waveshare WiThrottle Dial — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone, full single-loco JMRI WiThrottle throttle on the Waveshare ESP32-S3 Knob (360×360 round), forked from `withrottle_dial`, with roster browser, NVS recents, 270° speed arc, direction, functions, horn, and emergency stop.

**Architecture:** Fork `withrottle_dial` (ESP-IDF/CMake, LVGL 8.4, SquareLine GUI). Swap the `withr::` provider from the `espnow_dial` proxy to an extended `withrottle_client` that connects directly to JMRI. A light screen manager dispatches to per-screen modules; the UI reads cached state through the `withr::` getters each 50 ms tick and pushes commands to the existing FreeRTOS command queue.

**Tech Stack:** ESP-IDF 5.3.2, LVGL 8.4, `len0rd/WiThrottleProtocol` (submodule), FreeRTOS, NVS (via the `param` component), PlatformIO `native` + Unity for off-device unit tests.

**Reference checkouts (read-only, for porting):**
- Dial base: clone of `https://github.com/honzup/withrottle_dial`
- Feature reference: clone of `https://github.com/honzup/m5stack_wiThrottle`

**Spec:** `docs/superpowers/specs/2026-06-03-waveshare-withrottle-dial-design.md`

---

## Environment build & test commands (AUTHORITATIVE — overrides any `pio` command shown inside tasks)

This is a **native ESP-IDF project** (sources in `main/` + `components/`, built with `idf.py`). PlatformIO's `pio run`/`pio test` do **not** work here (PlatformIO expects a root `src/` dir and errors with "Missing the `src` folder"; the `pio` shim at `/home/john/.local/bin/pio` is also broken). Wherever a task below says a `pio …` command, use the real command instead:

| Task text says | Actually run |
|---|---|
| `pio run -e waveshare_knob` (firmware build verify) | `./build.sh build` |
| `pio run -e waveshare_knob_usb -t upload` / `pio device monitor` | **User only** — `./build.sh flash monitor` on the machine with the board |
| `pio test -e native -f <name>` | `bash test/native/run.sh <name>` |
| `pio test -e native` (all) | `bash test/native/run.sh` |

**Verification policy for this run (build + native tests; flashing is the user's):**
- **Native unit tests** (Phase 2 logic) run on **every** logic task — they are fast.
- **Firmware build** (`./build.sh build`, an `idf.py` build that is slow and pulls managed components on first run) runs **once per phase, at that phase's final task** — not on every micro-task — to keep iteration fast. A subagent may run it earlier if it wants extra confidence.
- Every **flash / serial-monitor** step is a checklist item handed to the user, never executed by a subagent.

The PlatformIO `[env:native]` described in the original Phase 1 is replaced by the standalone g++ + Unity harness defined in Task 1.1 below.

---

## File Structure

New or changed files this plan produces (relative to project root):

```
platformio.ini                     MODIFY  add [env:native] for Unity tests
CMakeLists.txt                     KEEP    (from dial fork)
partitions.csv / sdkconfig         KEEP
src/secrets.h.example              CREATE  committed template for compile-time defaults
src/secrets.h                      CREATE  git-ignored, real credentials

components/
  withrottle_client/               EXTEND  becomes the withr:: provider
    withrottle_client.h            MODIFY  add acquire/release/roster/phase/horn API
    withrottle_client.cpp          MODIFY  remove hardcoded "S3"; CMD_ACQUIRE/RELEASE; phase
    CMakeLists.txt                 KEEP
  loco_ref/                        CREATE  pure value types + address parsing (native-testable)
    include/loco_ref.h
    loco_ref.cpp
    CMakeLists.txt
  horn_resolver/                   CREATE  pure horn-index resolver (native-testable)
    include/horn_resolver.h
    horn_resolver.cpp
    CMakeLists.txt
  recents/                         CREATE  NVS-backed recents (pure serialise + Param persistence)
    include/recents.h
    recents.cpp
    recents_serialize.h            pure (de)serialise — native-testable, no ESP-IDF deps
    recents_serialize.cpp
    CMakeLists.txt
  ui_screens/                      CREATE  screen manager + per-screen modules
    include/screen_mgr.h
    screen_mgr.cpp
    screen_connecting.cpp
    screen_home.cpp
    screen_roster.cpp
    screen_recent.cpp
    screen_throttle.cpp            (was ui_events.cpp Train_Main logic, de-espnow'd)
    screen_functions.cpp           (wraps existing TrainFnScreen)
    screen_settings.cpp
    list_widget.h / list_widget.cpp  shared roster/recent list builder
    CMakeLists.txt
  espnow_dial/                     DELETE  proxy backend removed

main/
  main.cpp                         MODIFY  remove espnow, wire screen_mgr, route encoder
  ui_events.cpp                    REDUCE  move logic into ui_screens/screen_throttle.cpp
  train_fn_screen.{h,cpp}          KEEP    (reused by screen_functions.cpp)

test/                              CREATE  native Unity tests
  test_loco_ref/test_loco_ref.cpp
  test_horn_resolver/test_horn_resolver.cpp
  test_recents_serialize/test_recents_serialize.cpp
```

**Why these boundaries:** the pure-logic units (`loco_ref`, `horn_resolver`, `recents_serialize`) have zero ESP-IDF/LVGL dependencies so they compile and run under the `native` test env. Everything hardware/UI-bound is reached only through the `withr::` API or the screen-module interface, so the testable core never depends on hardware.

---

## Phase 0 — Fork the dial and establish a build baseline

Goal: get the unmodified dial firmware compiling and running on the board so the toolchain is proven before we change anything.

### Task 0.1: Import the dial source as the project base

**Files:**
- Create: all of `withrottle_dial`'s tree into the project root (alongside the existing `docs/` and `.gitignore`).

- [ ] **Step 1: Clone the dial with submodules into a temp dir**

```bash
rm -rf /tmp/dial_base
git clone --recurse-submodules https://github.com/honzup/withrottle_dial /tmp/dial_base
```

Expected: clone succeeds and `/tmp/dial_base/components/withrottle/src/WiThrottleProtocol.h` exists (the submodule populated).

- [ ] **Step 2: Copy the source tree into the project (preserve existing docs/.gitignore)**

```bash
cd /home/john/Documents/PlatformIO/Projects/waveshare_withrottle_dial
rsync -a --exclude='.git' --exclude='docs' --exclude='.gitignore' /tmp/dial_base/ ./
```

Expected: `main/`, `components/`, `platformio.ini`, `CMakeLists.txt`, `partitions.csv`, `sdkconfig` now present.

- [ ] **Step 3: Confirm the WiThrottleProtocol submodule came across**

Run: `ls components/withrottle/src/WiThrottleProtocol.h && ls components/withrottle/src/SocketStream.h`
Expected: both files listed. If missing, copy `/tmp/dial_base/components/withrottle/` in manually.

- [ ] **Step 4: Build the unmodified firmware**

Run: `pio run -e waveshare_knob`
Expected: build SUCCESS, produces `.pio/build/waveshare_knob/firmware.bin` (or `build/withrottle_knob.bin`). If the IDF toolchain is not yet installed PlatformIO will fetch it on first run.

- [ ] **Step 5: Flash and confirm it boots (manual, on-device)**

Run: `pio run -e waveshare_knob_usb -t upload && pio device monitor`
Expected: serial log shows `Starting WiThrottle Knob BUILD ...`, display lights up. (This is the ESP-NOW proxy build — it won't connect to JMRI yet. We only need it to boot.)

- [ ] **Step 6: Commit the baseline**

```bash
git add -A
git commit -m "Import withrottle_dial as project base"
```

---

## Phase 1 — Native test harness

Goal: a standalone g++ + Unity harness (PlatformIO-independent) so the pure-logic phases can be true TDD. Firmware build path is `./build.sh build` (idf.py); see the Environment commands section above.

### Task 1.1: Add a standalone native test harness

**Files:**
- Create: `test/native/unity/unity.c`, `unity.h`, `unity_internals.h` (vendored from the ESP-IDF framework copy)
- Create: `test/native/run.sh` (compile + run a Unity test with g++)
- Create: `test/test_smoke/test_smoke.cpp` (proves the harness works before Phase 2 sources exist)

- [ ] **Step 1: Vendor Unity**

Copy Unity's three source files out of the installed ESP-IDF framework:

```bash
mkdir -p test/native/unity
cp ~/.platformio/packages/framework-espidf/components/unity/unity/src/unity.c \
   ~/.platformio/packages/framework-espidf/components/unity/unity/src/unity.h \
   ~/.platformio/packages/framework-espidf/components/unity/unity/src/unity_internals.h \
   test/native/unity/
```

Expected: three files present in `test/native/unity/`. (If that path is absent, find it with `find ~/.platformio -name unity.c -path '*unity/src*' | head -1` and copy that file plus the two headers beside it.)

- [ ] **Step 2: Write the runner**

`test/native/run.sh`:

```bash
#!/usr/bin/env bash
# Standalone native test runner: compiles a Unity test with g++ and runs it.
# Usage: bash test/native/run.sh [test_name]   (no arg = run every test/test_*/)
set -euo pipefail
cd "$(dirname "$0")/../.."          # repo root
ROOT="$(pwd)"
UNITY="$ROOT/test/native/unity"
INCLUDES=(-I"$UNITY"
          -Icomponents/loco_ref/include
          -Icomponents/horn_resolver/include
          -Icomponents/recents)

# Pure (ESP-IDF-free) component sources — included if they exist yet.
PURE=()
for s in components/loco_ref/loco_ref.cpp \
         components/horn_resolver/horn_resolver.cpp \
         components/recents/recents_serialize.cpp; do
  [ -f "$s" ] && PURE+=("$s")
done

run_one() {
  local name="$1"
  local dir="test/$name"
  [ -d "$dir" ] || { echo "No such test dir: $dir"; return 2; }
  local bin; bin="$(mktemp -d)/t"
  echo "==> building $name"
  g++ -std=gnu++17 -Wall -Wextra "${INCLUDES[@]}" \
      "$dir"/*.cpp "${PURE[@]}" "$UNITY/unity.c" -o "$bin"
  echo "==> running $name"
  "$bin"
}

if [ $# -ge 1 ]; then
  # accept either "test_foo" or "foo"
  case "$1" in test_*) run_one "$1";; *) run_one "test_$1";; esac
else
  rc=0
  for d in test/test_*/; do run_one "$(basename "$d")" || rc=$?; done
  exit $rc
fi
```

Make it executable: `chmod +x test/native/run.sh`.

- [ ] **Step 3: Write a smoke test**

`test/test_smoke/test_smoke.cpp`:

```cpp
#include <unity.h>
void test_harness_works() { TEST_ASSERT_EQUAL_INT(4, 2 + 2); }
void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_harness_works);
    return UNITY_END();
}
```

- [ ] **Step 4: Run the smoke test**

Run: `bash test/native/run.sh test_smoke`
Expected: compiles and prints `1 Tests 0 Failures 0 Ignored` / `OK`.

- [ ] **Step 5: Commit**

```bash
git add test/native test/test_smoke
git commit -m "Add standalone native Unity test harness"
```

---

## Phase 2 — Pure-logic modules (TDD)

Goal: the testable core — address parsing, horn resolver, recents (de)serialisation — built test-first with no hardware deps.

### Task 2.1: `loco_ref` value type and address parsing

**Files:**
- Create: `components/loco_ref/include/loco_ref.h`
- Create: `components/loco_ref/loco_ref.cpp`
- Create: `components/loco_ref/CMakeLists.txt`
- Test: `test/test_loco_ref/test_loco_ref.cpp`

- [ ] **Step 1: Write the failing test**

`test/test_loco_ref/test_loco_ref.cpp`:

```cpp
#include <unity.h>
#include "loco_ref.h"

void test_parse_short_address() {
    LocoRef r;
    TEST_ASSERT_TRUE(loco_parse_address("S3", r));
    TEST_ASSERT_EQUAL_INT(3, r.address);
    TEST_ASSERT_EQUAL_CHAR('S', r.length);
}

void test_parse_long_address() {
    LocoRef r;
    TEST_ASSERT_TRUE(loco_parse_address("L1234", r));
    TEST_ASSERT_EQUAL_INT(1234, r.address);
    TEST_ASSERT_EQUAL_CHAR('L', r.length);
}

void test_parse_rejects_garbage() {
    LocoRef r;
    TEST_ASSERT_FALSE(loco_parse_address("X", r));
    TEST_ASSERT_FALSE(loco_parse_address("", r));
    TEST_ASSERT_FALSE(loco_parse_address("S", r));
}

void test_format_address_roundtrip() {
    TEST_ASSERT_EQUAL_STRING("S3",   loco_format_address(3, 'S').c_str());
    TEST_ASSERT_EQUAL_STRING("L1234", loco_format_address(1234, 'L').c_str());
}

void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_short_address);
    RUN_TEST(test_parse_long_address);
    RUN_TEST(test_parse_rejects_garbage);
    RUN_TEST(test_format_address_roundtrip);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash test/native/run.sh test_loco_ref`
Expected: FAIL — `loco_ref.h` not found / undefined references.

- [ ] **Step 3: Write the header**

`components/loco_ref/include/loco_ref.h`:

```cpp
#pragma once
#include <string>

// A reference to a locomotive by DCC address and address length.
struct LocoRef {
    int         address = 0;
    char        length  = 'L';   // 'S' (short) or 'L' (long)
    std::string name;            // display name; may be empty until roster loads
};

// Parse a WiThrottle address token like "S3" or "L1234" into out.
// Returns false if the token is malformed (too short, non-numeric tail).
bool loco_parse_address(const std::string& token, LocoRef& out);

// Format an address+length back into a WiThrottle token ("S3", "L1234").
std::string loco_format_address(int address, char length);
```

- [ ] **Step 4: Write the implementation**

`components/loco_ref/loco_ref.cpp`:

```cpp
#include "loco_ref.h"
#include <cctype>
#include <cstdlib>

bool loco_parse_address(const std::string& token, LocoRef& out) {
    if (token.size() < 2) return false;
    char len = token[0];
    if (len != 'S' && len != 'L') return false;
    for (size_t i = 1; i < token.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(token[i]))) return false;
    out.length  = len;
    out.address = std::atoi(token.c_str() + 1);
    return true;
}

std::string loco_format_address(int address, char length) {
    return std::string(1, length) + std::to_string(address);
}
```

- [ ] **Step 5: Write the component CMakeLists (for the firmware build)**

`components/loco_ref/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "loco_ref.cpp"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 6: Run test to verify it passes**

Run: `bash test/native/run.sh test_loco_ref`
Expected: PASS — 4 tests.

- [ ] **Step 7: Commit**

```bash
git add components/loco_ref test/test_loco_ref
git commit -m "Add loco_ref value type and address parsing with tests"
```

### Task 2.2: `horn_resolver`

**Files:**
- Create: `components/horn_resolver/include/horn_resolver.h`
- Create: `components/horn_resolver/horn_resolver.cpp`
- Create: `components/horn_resolver/CMakeLists.txt`
- Test: `test/test_horn_resolver/test_horn_resolver.cpp`

- [ ] **Step 1: Write the failing test**

`test/test_horn_resolver/test_horn_resolver.cpp`:

```cpp
#include <unity.h>
#include "horn_resolver.h"
#include <vector>
#include <string>

void test_finds_horn_label() {
    std::vector<std::string> names = {"Light", "Bell", "Horn", "Coupler"};
    TEST_ASSERT_EQUAL_INT(2, resolve_horn_fn(names));
}
void test_case_insensitive() {
    std::vector<std::string> names = {"", "AIR HORN", ""};
    TEST_ASSERT_EQUAL_INT(1, resolve_horn_fn(names));
}
void test_defaults_to_two_when_absent() {
    std::vector<std::string> names = {"Light", "Bell", "", "Steam"};
    TEST_ASSERT_EQUAL_INT(2, resolve_horn_fn(names));
}
void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_finds_horn_label);
    RUN_TEST(test_case_insensitive);
    RUN_TEST(test_defaults_to_two_when_absent);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash test/native/run.sh test_horn_resolver`
Expected: FAIL — header not found.

- [ ] **Step 3: Write the header**

`components/horn_resolver/include/horn_resolver.h`:

```cpp
#pragma once
#include <vector>
#include <string>

// Return the index of the first function name containing "horn"
// (case-insensitive), or 2 if none is found. Mirrors m5's resolve_horn_fn.
int resolve_horn_fn(const std::vector<std::string>& names);
```

- [ ] **Step 4: Write the implementation**

`components/horn_resolver/horn_resolver.cpp`:

```cpp
#include "horn_resolver.h"
#include <algorithm>
#include <cctype>

int resolve_horn_fn(const std::vector<std::string>& names) {
    for (size_t i = 0; i < names.size(); ++i) {
        std::string lower = names[i];
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        if (lower.find("horn") != std::string::npos)
            return static_cast<int>(i);
    }
    return 2;
}
```

- [ ] **Step 5: Write the component CMakeLists**

`components/horn_resolver/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "horn_resolver.cpp"
                       INCLUDE_DIRS "include")
```

- [ ] **Step 6: Run test to verify it passes**

Run: `bash test/native/run.sh test_horn_resolver`
Expected: PASS — 3 tests.

- [ ] **Step 7: Commit**

```bash
git add components/horn_resolver test/test_horn_resolver
git commit -m "Add horn function-index resolver with tests"
```

### Task 2.3: `recents_serialize` — pure (de)serialisation + dedup

Recents are persisted as a single string param: entries separated by `;`, fields by `:` — e.g. `S3:Big Boy;L1234:Shay`. Names may contain spaces but not `:` or `;` (sanitised on push).

**Files:**
- Create: `components/recents/recents_serialize.h`
- Create: `components/recents/recents_serialize.cpp`
- Test: `test/test_recents_serialize/test_recents_serialize.cpp`

- [ ] **Step 1: Write the failing test**

`test/test_recents_serialize/test_recents_serialize.cpp`:

```cpp
#include <unity.h>
#include "recents_serialize.h"

void test_serialize_roundtrip() {
    std::vector<LocoRef> in = {
        {3,   'S', "Big Boy"},
        {1234,'L', "Shay"},
    };
    std::string s = recents_serialize(in);
    std::vector<LocoRef> out = recents_deserialize(s);
    TEST_ASSERT_EQUAL_INT(2, out.size());
    TEST_ASSERT_EQUAL_INT(3, out[0].address);
    TEST_ASSERT_EQUAL_CHAR('S', out[0].length);
    TEST_ASSERT_EQUAL_STRING("Big Boy", out[0].name.c_str());
    TEST_ASSERT_EQUAL_INT(1234, out[1].address);
}

void test_deserialize_empty_is_empty() {
    TEST_ASSERT_EQUAL_INT(0, recents_deserialize("").size());
}

void test_push_dedups_by_address_and_length() {
    std::vector<LocoRef> list;
    recents_push(list, {3,'S',"Big Boy"}, 5);
    recents_push(list, {1234,'L',"Shay"}, 5);
    recents_push(list, {3,'S',"Big Boy II"}, 5);   // same addr+len → moves to front, updates name
    TEST_ASSERT_EQUAL_INT(2, list.size());
    TEST_ASSERT_EQUAL_INT(3, list[0].address);
    TEST_ASSERT_EQUAL_STRING("Big Boy II", list[0].name.c_str());
    TEST_ASSERT_EQUAL_INT(1234, list[1].address);
}

void test_push_trims_to_max() {
    std::vector<LocoRef> list;
    for (int i = 1; i <= 7; ++i) recents_push(list, {i,'L',"n"}, 5);
    TEST_ASSERT_EQUAL_INT(5, list.size());
    TEST_ASSERT_EQUAL_INT(7, list[0].address);   // most recent first
    TEST_ASSERT_EQUAL_INT(3, list[4].address);   // oldest kept
}

void test_short_address_distinct_from_long() {
    std::vector<LocoRef> list;
    recents_push(list, {3,'S',"a"}, 5);
    recents_push(list, {3,'L',"b"}, 5);
    TEST_ASSERT_EQUAL_INT(2, list.size());
}

void setUp() {} void tearDown() {}
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_serialize_roundtrip);
    RUN_TEST(test_deserialize_empty_is_empty);
    RUN_TEST(test_push_dedups_by_address_and_length);
    RUN_TEST(test_push_trims_to_max);
    RUN_TEST(test_short_address_distinct_from_long);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash test/native/run.sh test_recents_serialize`
Expected: FAIL — header not found.

- [ ] **Step 3: Write the header**

`components/recents/recents_serialize.h`:

```cpp
#pragma once
#include <string>
#include <vector>
#include "loco_ref.h"

// Serialise recents to "S3:Name;L1234:Name". Names are emitted verbatim;
// callers must strip ':' and ';' before pushing (see recents_sanitize_name).
std::string recents_serialize(const std::vector<LocoRef>& list);

// Parse the serialised form. Malformed entries are skipped.
std::vector<LocoRef> recents_deserialize(const std::string& blob);

// Insert entry at the front, dedup by (address,length), trim to max_entries.
// If an existing entry matches, it is moved to the front and its name updated.
void recents_push(std::vector<LocoRef>& list, const LocoRef& entry, size_t max_entries);

// Replace ':' and ';' with spaces so a name is safe to serialise.
std::string recents_sanitize_name(const std::string& name);
```

- [ ] **Step 4: Write the implementation**

`components/recents/recents_serialize.cpp`:

```cpp
#include "recents_serialize.h"
#include <sstream>

std::string recents_sanitize_name(const std::string& name) {
    std::string out = name;
    for (char& c : out) if (c == ':' || c == ';') c = ' ';
    return out;
}

std::string recents_serialize(const std::vector<LocoRef>& list) {
    std::string out;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i) out += ';';
        out += loco_format_address(list[i].address, list[i].length);
        out += ':';
        out += list[i].name;
    }
    return out;
}

std::vector<LocoRef> recents_deserialize(const std::string& blob) {
    std::vector<LocoRef> out;
    std::stringstream ss(blob);
    std::string entry;
    while (std::getline(ss, entry, ';')) {
        if (entry.empty()) continue;
        size_t colon = entry.find(':');
        std::string token = (colon == std::string::npos) ? entry : entry.substr(0, colon);
        std::string name  = (colon == std::string::npos) ? ""    : entry.substr(colon + 1);
        LocoRef r;
        if (!loco_parse_address(token, r)) continue;
        r.name = name;
        out.push_back(r);
    }
    return out;
}

void recents_push(std::vector<LocoRef>& list, const LocoRef& entry, size_t max_entries) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].address == entry.address && list[i].length == entry.length) {
            list.erase(list.begin() + i);
            break;
        }
    }
    LocoRef e = entry;
    e.name = recents_sanitize_name(entry.name);
    list.insert(list.begin(), e);
    if (list.size() > max_entries) list.resize(max_entries);
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `bash test/native/run.sh test_recents_serialize`
Expected: PASS — 5 tests.

- [ ] **Step 6: Commit**

```bash
git add components/recents/recents_serialize.* test/test_recents_serialize
git commit -m "Add recents serialise/dedup pure logic with tests"
```

---

## Phase 3 — Swap the `withr::` provider to a direct JMRI client

Goal: replace the ESP-NOW proxy with the extended `withrottle_client`, so the firmware connects to JMRI and a single loco is drivable. The hardcoded `"S3"` stays as a temporary default until Phase 5 wires roster selection.

### Task 3.1: Extend the `withr::` API surface

**Files:**
- Modify: `components/withrottle_client/withrottle_client.h`

- [ ] **Step 1: Add the new declarations**

In `components/withrottle_client/withrottle_client.h`, inside `namespace withr`, add (keep all existing declarations):

```cpp
#include <vector>
#include "loco_ref.h"

// --- Connection phase (for the connecting screen) ---
enum class Phase { WIFI, SERVER, ROSTER, READY };
Phase get_phase();
bool  roster_ready();

// --- Roster access for the UI ---
std::vector<LocoRef> get_roster();    // snapshot, sorted by name (case-insensitive)

// --- Loco lifecycle (replaces hardcoded acquire) ---
void acquire_loco(int address, char length);
void release_loco();
bool has_loco();

// --- Horn ---
int  get_horn_fn();        // resolved index for the active loco (default 2)
void horn(bool on);        // convenience: set_function(get_horn_fn(), on)
```

- [ ] **Step 2: Add the `loco_ref` dependency to the component build**

In `components/withrottle_client/CMakeLists.txt`, add `loco_ref`, `horn_resolver` and `recents` to `REQUIRES` (or `PRIV_REQUIRES`). Read the existing file first; it currently registers against `withrottle`, `wifi`, `param`. New form:

```cmake
idf_component_register(SRCS "withrottle_client.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES withrottle wifi param loco_ref horn_resolver)
```

- [ ] **Step 3: Build to confirm the header compiles (impl added next task)**

Run: `pio run -e waveshare_knob`
Expected: FAIL at link with undefined references to the new `withr::` functions (header parses, no impl yet). This confirms the header is well-formed.

- [ ] **Step 4: Commit**

```bash
git add components/withrottle_client/withrottle_client.h components/withrottle_client/CMakeLists.txt
git commit -m "Declare extended withr:: API (phase, roster, acquire/release, horn)"
```

### Task 3.2: Implement phase, roster snapshot, acquire/release, horn

**Files:**
- Modify: `components/withrottle_client/withrottle_client.cpp`

- [ ] **Step 1: Add command types and a roster mutex**

In `withrottle_client.cpp`, extend the `withr_cmd_t` enum with `CMD_ACQUIRE` and `CMD_RELEASE`. Add to the struct: `int acq_address; char acq_length;`. Add a file-scope `static SemaphoreHandle_t s_roster_mutex = nullptr;` created in `client_start()` (`s_roster_mutex = xSemaphoreCreateMutex();`). Add `static std::atomic<bool> s_roster_ready{false};` and include `<atomic>`.

```cpp
typedef struct {
    enum { CMD_SPEED, CMD_DIR, CMD_ESTOP, CMD_FUNC, CMD_ACQUIRE, CMD_RELEASE } type;
    int   value;
    bool  func_state;
    int   acq_address;
    char  acq_length;
    char  throttle = DEFAULT_MULTITHROTTLE;
} withr_cmd_t;
```

- [ ] **Step 2: Remove the hardcoded acquire from the task; keep a build-time default**

In `withrottle_task`, replace the unconditional `s_wiThrottle.addLocomotive(DEFAULT_MULTITHROTTLE, WITHR_LOCO_ADDR);` after session start with: only acquire the default loco if no loco has been requested yet (so Phase 3 still drives something). Guard with a `static bool s_acquired_default = false;` and a file-scope `s_current_addr`/`s_current_len`:

```cpp
// After setDeviceName(...):
if (!withr::has_loco()) {
    s_wiThrottle.addLocomotive(DEFAULT_MULTITHROTTLE, WITHR_LOCO_ADDR);  // temp default
}
```

(The `WITHR_LOCO_ADDR "S3"` define is removed in Phase 5 once roster selection lands.)

- [ ] **Step 3: Handle CMD_ACQUIRE / CMD_RELEASE in the command drain loop**

In the `xQueueReceive` switch inside `withrottle_task`, add:

```cpp
case withr_cmd_t::CMD_ACQUIRE: {
    std::string token = loco_format_address(cmd.acq_address, cmd.acq_length);
    if (s_has_loco) s_wiThrottle.releaseLocomotive(DEFAULT_MULTITHROTTLE);
    s_wiThrottle.addLocomotive(DEFAULT_MULTITHROTTLE, String(token.c_str()));
    s_current_addr = cmd.acq_address;
    s_current_len  = cmd.acq_length;
    break;
}
case withr_cmd_t::CMD_RELEASE:
    if (s_has_loco) s_wiThrottle.releaseLocomotive(DEFAULT_MULTITHROTTLE);
    break;
```

Add file-scope `static volatile bool s_has_loco = false; static int s_current_addr = 0; static char s_current_len = 'L';`. Set `s_has_loco = true` in the delegate's `addressAddedMultiThrottle`, and `false` in `addressRemovedMultiThrottle`.

- [ ] **Step 4: Set `roster_ready` and store the function-name list for horn resolution**

In the delegate's `receivedRosterFunctionListMultiThrottle`, after copying names, mark the roster usable: there is no explicit "roster complete" event, so treat `roster_ready` as true once at least one `receivedRosterEntry` has arrived. In `receivedRosterEntry`, set `s_roster_ready = true;`.

In the delegate, after the active loco's function list is stored, compute and cache the horn index:

```cpp
// inside receivedRosterFunctionListMultiThrottle, after filling info.fns[].name:
std::vector<std::string> names;
for (size_t k = 0; k < MAX_FUNCTIONS; ++k) names.push_back(info.fns[k].name);
s_horn_fn = resolve_horn_fn(names);
```

Add `#include "horn_resolver.h"` and a file-scope `static std::atomic<int> s_horn_fn{2};`.

- [ ] **Step 5: Implement the new public functions**

Append to `namespace withr`:

> **Correctness note:** the existing `s_connected` flag is set in `addressAddedMultiThrottle` / cleared in `addressRemovedMultiThrottle`, so it actually means **"a loco is acquired"**, NOT "TCP session up". Reusing it for the SERVER→ROSTER transition would stall the connecting screen at SERVER until a loco is acquired (which only happens after the user picks one). So introduce a **separate** `static std::atomic<bool> s_session_up{false};` — set `true` right after `s_wiThrottle.connect(&client, 100)` succeeds in `withrottle_task`, and `false` on disconnect (the `client.disconnect()` cleanup path). `has_loco()`/`is_connected()` keep their "loco acquired" meaning for the speed/function getters.

```cpp
Phase get_phase() {
    if (!is_wifi_connected())      return Phase::WIFI;
    if (!s_session_up.load())      return Phase::SERVER;  // set after s_wiThrottle.connect() succeeds
    if (!s_roster_ready.load())    return Phase::ROSTER;
    return Phase::READY;
}

bool roster_ready() { return s_roster_ready.load(); }

std::vector<LocoRef> get_roster() {
    std::vector<LocoRef> out;
    if (s_roster_mutex) xSemaphoreTake(s_roster_mutex, portMAX_DELAY);
    for (const auto& [addr, info] : s_delegate.roster)
        out.push_back(LocoRef{addr, info.length, info.name});
    if (s_roster_mutex) xSemaphoreGive(s_roster_mutex);
    std::sort(out.begin(), out.end(), [](const LocoRef& a, const LocoRef& b){
        std::string la = a.name, lb = b.name;
        std::transform(la.begin(), la.end(), la.begin(), ::tolower);
        std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
        return la < lb;
    });
    return out;
}

void acquire_loco(int address, char length) {
    withr_cmd_t cmd{}; cmd.type = withr_cmd_t::CMD_ACQUIRE;
    cmd.acq_address = address; cmd.acq_length = length;
    xQueueSend(s_cmd_queue, &cmd, 0);
}
void release_loco() {
    withr_cmd_t cmd{}; cmd.type = withr_cmd_t::CMD_RELEASE;
    xQueueSend(s_cmd_queue, &cmd, 0);
}
bool has_loco() { return s_has_loco; }

int  get_horn_fn() { return s_horn_fn.load(); }
void horn(bool on) { set_function(get_horn_fn(), on); }
```

Add `#include <algorithm>`. Guard delegate roster mutations (`receivedRosterEntry`, `addressAdded*`, `receivedRosterFunctionList*`, `receivedFunctionState*`) with `s_roster_mutex` take/give to keep `get_roster()` consistent.

- [ ] **Step 6: Build**

Run: `pio run -e waveshare_knob`
Expected: build SUCCESS (still links `espnow_dial` too — provider swap happens in Task 3.3, so expect a duplicate-symbol link error for `namespace withr`). If the duplicate-symbol error appears, that is expected and resolved in the next task; otherwise SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add components/withrottle_client/withrottle_client.cpp
git commit -m "Implement withr:: phase, roster snapshot, acquire/release, horn"
```

### Task 3.3: Remove the ESP-NOW provider and point the build at the direct client

**Files:**
- Delete: `components/espnow_dial/`
- Modify: `main/main.cpp` (includes + `withr::client_start()` already called)
- Modify: `main/ui_events.cpp`, `main/train_fn_screen.h` (replace `#include "espnow_dial.h"`)

- [ ] **Step 1: Delete the proxy component**

```bash
git rm -r components/espnow_dial
```

- [ ] **Step 2: Repoint includes from espnow to the direct client**

Replace every `#include "espnow_dial.h"` with `#include "withrottle_client.h"` in: `main/ui_events.cpp`, `main/train_fn_screen.h`, `main/main.cpp` (and anywhere else `grep -rl espnow_dial main components` reports).

Run first: `grep -rl espnow_dial main components`
Then edit each file. `Direction` and `MAX_FUNCTIONS` come from `WiThrottleProtocol.h`, which `withrottle_client.h` already includes.

- [ ] **Step 3: Confirm `main.cpp` no longer references espnow**

Remove `#include "espnow_dial.h"` and any `espnow_*` calls from `main/main.cpp`. `withr::client_start();` is already present and remains.

- [ ] **Step 4: Update the main component's REQUIRES**

In `main/CMakeLists.txt`, remove `espnow_dial` from `REQUIRES` and ensure `withrottle_client` is listed. Read the file first to preserve the rest.

- [ ] **Step 5: Build**

Run: `pio run -e waveshare_knob`
Expected: SUCCESS now that only one `namespace withr` provider remains. Remaining errors will be in `ui_events.cpp` for the badge/auto-follow calls — fixed in Task 3.4.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Remove espnow_dial proxy; build against direct withrottle_client"
```

### Task 3.4: De-ESP-NOW the throttle screen (drop AUTO/A/B badge)

**Files:**
- Modify: `main/ui_events.cpp`

- [ ] **Step 1: Remove the throttle-slot badge and auto-follow code**

In `main/ui_events.cpp`:
- Delete `throttle_badge_clicked`, `update_throttle_badge`, `s_throttle_badge`, `s_throttle_label`, `s_badge_last_auto`, `s_badge_last_slot`, and the badge-creation block in `onTrainMainControlLoaded`.
- In `onTrainMainControlUnloaded`, remove the badge cleanup lines.
- Keep `s_horn_btn`, `horn_btn_event` (it calls `withr::horn(...)`, now provided by the direct client), `s_battery_label`, `update_battery_label`, STOP/Fn/Dir/back wiring.

- [ ] **Step 2: Remove the badge call from `main.cpp`**

In `main/main.cpp::update_main_control_state()`, delete the `update_throttle_badge();` call and its forward declaration at the top of the file.

- [ ] **Step 3: Build**

Run: `pio run -e waveshare_knob`
Expected: SUCCESS.

- [ ] **Step 4: Flash and verify a loco is drivable (manual, on-device)**

Set credentials over serial first:
```
param set wifi_ssid "YourSSID"
param set wifi_password "YourPass"
param set withr_ip "<JMRI host>"
param save
```
Run: `pio run -e waveshare_knob_usb -t upload && pio device monitor`
Expected: log shows `Connecting to <host>:12090`, `WiThrottle session started`, `Loco acquired: S3`. On the Train Main screen, rotating the encoder changes speed (JMRI throttle moves), STOP works, HORN sounds, Fn screen lists functions. (Loco selection still defaults to S3 — roster UI comes in Phase 5.)

- [ ] **Step 5: Commit**

```bash
git add main/ui_events.cpp main/main.cpp
git commit -m "Drop ESP-NOW throttle-slot badge from throttle screen"
```

---

## Phase 4 — Screen manager and input routing

Goal: a small screen manager and a clean mapping from encoder rotation to the active screen, replacing the inline if/else in `main.cpp`.

### Task 4.1: Add the screen manager

**Files:**
- Create: `components/ui_screens/include/screen_mgr.h`
- Create: `components/ui_screens/screen_mgr.cpp`
- Create: `components/ui_screens/CMakeLists.txt`

- [ ] **Step 1: Write the header**

`components/ui_screens/include/screen_mgr.h`:

```cpp
#pragma once

enum class Screen { CONNECTING, HOME, ROSTER, RECENT, THROTTLE, FUNCTIONS, SETTINGS };

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
```

- [ ] **Step 2: Write the implementation**

`components/ui_screens/screen_mgr.cpp`:

```cpp
#include "screen_mgr.h"
#include <cstddef>

// Each screen file defines and registers its module via this table.
extern ScreenModule g_screen_connecting;
extern ScreenModule g_screen_home;
extern ScreenModule g_screen_roster;
extern ScreenModule g_screen_recent;
extern ScreenModule g_screen_throttle;
extern ScreenModule g_screen_functions;
extern ScreenModule g_screen_settings;

static ScreenModule* module_for(Screen s) {
    switch (s) {
        case Screen::CONNECTING: return &g_screen_connecting;
        case Screen::HOME:       return &g_screen_home;
        case Screen::ROSTER:     return &g_screen_roster;
        case Screen::RECENT:     return &g_screen_recent;
        case Screen::THROTTLE:   return &g_screen_throttle;
        case Screen::FUNCTIONS:  return &g_screen_functions;
        case Screen::SETTINGS:   return &g_screen_settings;
    }
    return nullptr;
}

static Screen s_current = Screen::CONNECTING;

void screen_mgr_init() {
    s_current = Screen::CONNECTING;
    if (module_for(s_current)->enter) module_for(s_current)->enter();
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
```

- [ ] **Step 3: Write the component CMakeLists (sources grow as screens are added)**

`components/ui_screens/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "screen_mgr.cpp"
         "screen_connecting.cpp" "screen_home.cpp" "screen_roster.cpp"
         "screen_recent.cpp" "screen_throttle.cpp" "screen_functions.cpp"
         "screen_settings.cpp" "list_widget.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ui withrottle_client recents loco_ref withrottle main_headers)
```

Note: the screens reference generated symbols (`ui_*`) from the `ui` component and `TrainFnScreen` from `main/`. To avoid a circular `main` dependency, move `train_fn_screen.{h,cpp}` into this component in Task 5.6. Until all screen source files exist, this build will fail to find them — that is expected; each subsequent task adds one file. To keep the tree buildable between tasks, create empty stubs now:

```bash
for f in screen_connecting screen_home screen_roster screen_recent \
         screen_throttle screen_functions screen_settings list_widget; do
  printf '#include "screen_mgr.h"\n' > components/ui_screens/$f.cpp
done
```

Add a placeholder definition in each stub so the externs resolve, e.g. in `screen_connecting.cpp`: `ScreenModule g_screen_connecting{};` (repeat per file with the matching symbol). Replace stubs with real code in Phase 5.

- [ ] **Step 4: Build**

Run: `pio run -e waveshare_knob`
Expected: SUCCESS (empty modules, manager not yet driven from main).

- [ ] **Step 5: Commit**

```bash
git add components/ui_screens
git commit -m "Add screen manager skeleton with empty screen modules"
```

### Task 4.2: Drive the screen manager from `main.cpp`

**Files:**
- Modify: `main/main.cpp`

- [ ] **Step 1: Replace the inline screen if/else with manager dispatch**

In `main/main.cpp`, inside `ui_update_task`, after acquiring the LVGL lock, replace the `if (act_scr == ui_Settings_Screen) … else if …` block and the per-screen scroll handling with:

```cpp
screen_mgr_tick();
int scroll_amount  = scroll_accumulator.load();
scroll_accumulator = 0;
if (scroll_amount != 0) screen_mgr_on_rotate(scroll_amount);
```

Add `#include "screen_mgr.h"` at the top. In `app_main`, after `ui_init();` and the encoder init, call `screen_mgr_init();` (before starting `ui_update_task`).

- [ ] **Step 2: Add `ui_screens` to the main component REQUIRES**

In `main/CMakeLists.txt`, add `ui_screens` to `REQUIRES`.

- [ ] **Step 3: Build and flash (manual)**

Run: `pio run -e waveshare_knob && pio run -e waveshare_knob_usb -t upload`
Expected: builds and boots. Screens won't navigate yet (modules empty), but no crash; serial stays healthy. Throttle driving via the still-present generated screens may be inert until Phase 5 — acceptable mid-phase.

- [ ] **Step 4: Commit**

```bash
git add main/main.cpp main/CMakeLists.txt
git commit -m "Drive screen manager and encoder routing from main loop"
```

---

## Phase 5 — Screen modules

Goal: implement each screen module against the `withr::` API, reusing the SquareLine-generated objects for existing screens and building the two new screens (Connecting, Recent) in matching LVGL C.

> Before each task that touches a generated screen, read the matching header to confirm object names, e.g. `components/ui/generated/screens/ui_Select_Train_Screen.h`. The names used below come from `ui_events.cpp`/`train_fn_screen.cpp` and the generated tree; verify and adjust if the SquareLine export differs.

### Phase 5 integration pattern (READ FIRST — applies to every screen module)

How the generated SquareLine UI and our `screen_mgr` fit together (verified against the generated sources):

1. **All screens are pre-created at `ui_init()`** — `ui_Main_Screen`, `ui_Settings_Screen`, `ui_Select_Train_Screen`, `ui_Train_Main_Control`, `ui_Train_Fn_Control` are all non-null after `ui_init()`. So a module's `enter()` can call `lv_scr_load(ui_X)` directly; never call `ui_X_screen_init()` yourself.
2. **`screen_mgr` owns navigation** — its internal `s_current` is the source of truth, and `screen_mgr_tick()`/`on_rotate()` dispatch to the module for `s_current` (NOT to `lv_scr_act()`). Therefore every screen change must go through `screen_mgr_show()`.
3. **The generated screens have baked-in navigation** — each nav button has a generated `ui_event_<Name>` callback (added with `LV_EVENT_ALL`) that calls `_ui_screen_change(&ui_Target, …)`. Some targets are WRONG for us (notably `ui_event_Select_Train_Btn` jumps to `ui_Train_Main_Control`, not the roster). **Rule:** for every nav button a module repurposes, in that module's one-time wiring do:
   ```cpp
   lv_obj_remove_event_cb(btn, ui_event_<Name>);   // drop the generated _ui_screen_change
   lv_obj_add_event_cb(btn, my_handler, LV_EVENT_CLICKED, nullptr); // our handler → screen_mgr_show(...) / action
   ```
   Guard the rebind with a `static bool` so it happens once (the generated objects persist across visits).
4. **The Connecting and Recent screens are NOT generated** — `screen_mgr` modules create them with `lv_obj_create(nullptr)` and own them.

**Authoritative navigation map (target screens are ours, via `screen_mgr_show`):**

| Screen | Button (object) | Generated target → our target |
|---|---|---|
| Home (`ui_Main_Screen`) | `ui_Select_Train_Btn` (`ui_event_Select_Train_Btn`) | Train_Main_Control → **ROSTER** (remove generated cb, rebind) |
| Home | `ui_Settings_Btn` (`ui_event_Settings_Btn`) | Settings → **SETTINGS** (rebind so `s_current` updates) |
| Home | *(none — create a Recents button)* | → **RECENT** |
| Settings | `ui_Settings_Back_Btn` (`ui_event_Settings_Back_Btn`) | Main → **HOME** (rebind) |
| Roster (`ui_Select_Train_Screen`) | its generated back btn | Main → **HOME** (rebind) |
| Roster | list rows (from `list_widget`) | — → acquire + **THROTTLE** |
| Throttle (`ui_Train_Main_Control`) | `ui_Train_Main_Control_Back_Btn` | Main → release_loco() + **HOME** (rebind) |
| Throttle | `ui_Train_Main_Fn_Btn` | Train_Fn_Control → **FUNCTIONS** (rebind) |
| Functions (`ui_Train_Fn_Control`) | its generated back btn | Train_Main_Control → **THROTTLE** (rebind) |

(If you can't find a screen's back-button object name in its generated header, read the screen's `.c` for the `ui_event_*` / `lv_obj_add_event_cb` lines.)

### Task 5.1: Shared list widget (roster + recents)

**Files:**
- Create: `components/ui_screens/list_widget.h`
- Modify: `components/ui_screens/list_widget.cpp` (replace stub)

- [ ] **Step 1: Write the header**

`components/ui_screens/list_widget.h`:

```cpp
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
```

- [ ] **Step 2: Write the implementation**

`components/ui_screens/list_widget.cpp`:

```cpp
#include "list_widget.h"
#include "ui.h"   // theme colours / fonts
#include <new>

static void (*s_on_select)(const LocoRef&) = nullptr;
// Heap-owned copies so the callback can read the chosen entry after the click.
static std::vector<LocoRef>* s_items = nullptr;

static void row_clicked(lv_event_t* e) {
    intptr_t idx = (intptr_t) lv_event_get_user_data(e);
    if (s_on_select && s_items && idx >= 0 && idx < (intptr_t) s_items->size())
        s_on_select((*s_items)[idx]);
}

void list_widget_populate(lv_obj_t* container,
                          const std::vector<LocoRef>& items,
                          void (*on_select)(const LocoRef&)) {
    if (!container) return;
    s_on_select = on_select;
    delete s_items;
    s_items = new (std::nothrow) std::vector<LocoRef>(items);

    lv_obj_clean(container);
    lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);

    for (size_t i = 0; i < items.size(); ++i) {
        lv_obj_t* btn = lv_btn_create(container);
        lv_obj_set_size(btn, 300, 56);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        ui_object_set_themeable_style_property(btn, LV_PART_MAIN | LV_STATE_DEFAULT,
                                               LV_STYLE_BG_COLOR, _ui_theme_color_Secondary);
        lv_obj_t* lbl = lv_label_create(btn);
        const LocoRef& r = items[i];
        std::string text = r.name.empty()
            ? loco_format_address(r.address, r.length)
            : r.name + "  (" + loco_format_address(r.address, r.length) + ")";
        lv_label_set_text(lbl, text.c_str());
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, row_clicked, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}
```

- [ ] **Step 3: Build**

Run: `pio run -e waveshare_knob`
Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add components/ui_screens/list_widget.*
git commit -m "Add shared loco list widget for roster and recents"
```

### Task 5.2: Connecting screen (new)

**Files:**
- Modify: `components/ui_screens/screen_connecting.cpp` (replace stub)

- [ ] **Step 1: Implement the module**

`components/ui_screens/screen_connecting.cpp`:

```cpp
#include "screen_mgr.h"
#include "withrottle_client.h"
#include "lvgl.h"

static lv_obj_t* s_scr   = nullptr;
static lv_obj_t* s_label = nullptr;

static const char* phase_text(withr::Phase p) {
    switch (p) {
        case withr::Phase::WIFI:   return "Connecting Wi-Fi…";
        case withr::Phase::SERVER: return "Connecting to JMRI…";
        case withr::Phase::ROSTER: return "Loading roster…";
        case withr::Phase::READY:  return "Ready";
    }
    return "";
}

static void enter() {
    if (!s_scr) {
        s_scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t* spin = lv_spinner_create(s_scr, 1000, 60);
        lv_obj_set_size(spin, 100, 100);
        lv_obj_align(spin, LV_ALIGN_CENTER, 0, -30);
        s_label = lv_label_create(s_scr);
        lv_obj_set_style_text_font(s_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_label, lv_color_hex(0xE3E3E3), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 70);
    }
    lv_scr_load(s_scr);
}

static void tick() {
    withr::Phase p = withr::get_phase();
    if (s_label) lv_label_set_text(s_label, phase_text(p));
    if (p == withr::Phase::READY) screen_mgr_show(Screen::HOME);
}

ScreenModule g_screen_connecting{ enter, nullptr, tick, nullptr };
```

- [ ] **Step 2: Build, flash, verify (manual)**

Run: `pio run -e waveshare_knob && pio run -e waveshare_knob_usb -t upload && pio device monitor`
Expected: on boot the spinner shows, label steps Wi-Fi → JMRI → roster, then switches to Home once a roster entry arrives.

- [ ] **Step 3: Commit**

```bash
git add components/ui_screens/screen_connecting.cpp
git commit -m "Add connecting screen driven by withr::get_phase()"
```

### Task 5.3: Home screen (menu → Roster / Recents / Settings)

**Files:**
- Modify: `components/ui_screens/screen_home.cpp` (replace stub)
- Read first: `components/ui/generated/screens/ui_Main_Screen.h`

- [ ] **Step 1: Identify the Main_Screen buttons**

Run: `sed -n '1,60p' components/ui/generated/screens/ui_Main_Screen.h`
Note the button object names (e.g. a roster/select button and a settings button). If `Main_Screen` lacks a dedicated Recents button, this task adds one in LVGL C.

- [ ] **Step 2: Implement the module**

`components/ui_screens/screen_home.cpp` — load `ui_Main_Screen`, attach click handlers that call `screen_mgr_show(Screen::ROSTER/RECENT/SETTINGS)`. Add a Recents button if absent. Skeleton (adjust object names to the header read in Step 1):

```cpp
#include "screen_mgr.h"
#include "ui.h"

static void go_roster(lv_event_t*)   { screen_mgr_show(Screen::ROSTER); }
static void go_recent(lv_event_t*)   { screen_mgr_show(Screen::RECENT); }
static void go_settings(lv_event_t*) { screen_mgr_show(Screen::SETTINGS); }

static bool s_wired = false;
static void enter() {
    if (!s_wired) {
        // TODO replace with real generated names confirmed in Step 1:
        // lv_obj_add_event_cb(ui_Main_Roster_Btn,   go_roster,   LV_EVENT_CLICKED, nullptr);
        // lv_obj_add_event_cb(ui_Main_Settings_Btn, go_settings, LV_EVENT_CLICKED, nullptr);
        // + create a Recents button and bind go_recent.
        s_wired = true;
    }
    lv_scr_load(ui_Main_Screen);
}
ScreenModule g_screen_home{ enter, nullptr, nullptr, nullptr };
```

> The `TODO` here is a read-then-fill instruction tied to Step 1's output, not an open-ended placeholder: bind the three buttons using the exact names from `ui_Main_Screen.h`, and create + bind a Recents button if one is not present.

- [ ] **Step 3: Build, flash, verify (manual)**

Expected: Home shows; tapping each entry navigates to the right screen (Roster/Recent/Settings show, even if their contents land in later tasks).

- [ ] **Step 4: Commit**

```bash
git add components/ui_screens/screen_home.cpp
git commit -m "Add home menu navigation"
```

### Task 5.4: Roster screen (populate from JMRI + select)

**Files:**
- Modify: `components/ui_screens/screen_roster.cpp` (replace stub)
- Read first: `components/ui/generated/screens/ui_Select_Train_Screen.h` (confirm `ui_Select_Train_Screen`, `ui_Train_Select_Container`)

- [ ] **Step 1: Implement the module**

`components/ui_screens/screen_roster.cpp`:

```cpp
#include "screen_mgr.h"
#include "withrottle_client.h"
#include "recents.h"
#include "list_widget.h"
#include "ui.h"

static void on_pick(const LocoRef& r) {
    withr::acquire_loco(r.address, r.length);
    recents_store_push(r);                 // defined in recents component (Task 5.5)
    screen_mgr_show(Screen::THROTTLE);
}

static uint32_t s_last_count = 0;
static void enter() {
    s_last_count = 0;                      // force a repopulate on entry
    lv_scr_load(ui_Select_Train_Screen);
}
static void tick() {
    // Repopulate when the roster size changes (entries stream in after connect).
    auto roster = withr::get_roster();
    if (roster.size() != s_last_count) {
        s_last_count = roster.size();
        list_widget_populate(ui_Train_Select_Container, roster, on_pick);
    }
}
static void on_rotate(int delta) {
    lv_obj_scroll_by(ui_Train_Select_Container, 0, delta, LV_ANIM_ON);
}
ScreenModule g_screen_roster{ enter, nullptr, tick, on_rotate };
```

- [ ] **Step 2: Build, flash, verify (manual)**

Expected: Roster lists JMRI locos (sorted by name); encoder scrolls; tapping one acquires it, records a recent, and jumps to the Throttle screen driving that loco.

- [ ] **Step 3: Commit**

```bash
git add components/ui_screens/screen_roster.cpp
git commit -m "Populate roster screen from JMRI and acquire on tap"
```

### Task 5.5: Recents component (NVS persistence) + recent screen

**Files:**
- Create: `components/recents/include/recents.h`
- Modify: `components/recents/recents.cpp` (new file; the serialize file already exists)
- Modify: `components/recents/CMakeLists.txt`
- Modify: `components/ui_screens/screen_recent.cpp` (replace stub)

- [ ] **Step 1: Write the persistence header**

`components/recents/include/recents.h`:

```cpp
#pragma once
#include "loco_ref.h"
#include <vector>

constexpr size_t RECENTS_MAX = 5;

// Load recents from NVS (param "recents"). Safe to call repeatedly.
std::vector<LocoRef> recents_load();

// Push a loco to the front (dedup + trim) and persist to NVS.
void recents_store_push(const LocoRef& r);
```

- [ ] **Step 2: Write the persistence implementation**

`components/recents/recents.cpp`:

```cpp
#include "recents.h"
#include "recents_serialize.h"
#include "Param.h"

static params::Param<std::string>& blob() {
    static params::Param<std::string> p{"recents", std::string("")};
    return p;
}

std::vector<LocoRef> recents_load() {
    return recents_deserialize(blob().get());
}

void recents_store_push(const LocoRef& r) {
    std::vector<LocoRef> list = recents_deserialize(blob().get());
    recents_push(list, r, RECENTS_MAX);
    blob().set(recents_serialize(list));   // set() persists to NVS
}
```

- [ ] **Step 3: Update the recents component CMakeLists**

`components/recents/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "recents_serialize.cpp" "recents.cpp"
                       INCLUDE_DIRS "include" "."
                       REQUIRES loco_ref param)
```

- [ ] **Step 4: Implement the recent screen**

`components/ui_screens/screen_recent.cpp` — build a new screen (no SquareLine source) that reuses `list_widget`:

```cpp
#include "screen_mgr.h"
#include "withrottle_client.h"
#include "recents.h"
#include "list_widget.h"
#include "lvgl.h"

static lv_obj_t* s_scr = nullptr;
static lv_obj_t* s_container = nullptr;

static void on_pick(const LocoRef& r) {
    withr::acquire_loco(r.address, r.length);
    recents_store_push(r);
    screen_mgr_show(Screen::THROTTLE);
}
static void enter() {
    if (!s_scr) {
        s_scr = lv_obj_create(nullptr);
        lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        s_container = lv_obj_create(s_scr);
        lv_obj_set_size(s_container, 340, 320);
        lv_obj_center(s_container);
        lv_obj_set_style_bg_opa(s_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(s_container, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    list_widget_populate(s_container, recents_load(), on_pick);
    lv_scr_load(s_scr);
}
static void on_rotate(int delta) {
    if (s_container) lv_obj_scroll_by(s_container, 0, delta, LV_ANIM_ON);
}
ScreenModule g_screen_recent{ enter, nullptr, nullptr, on_rotate };
```

- [ ] **Step 5: Build, flash, verify (manual)**

Expected: after acquiring one or two locos via roster, the Recents screen lists them most-recent first; tapping re-acquires. Power-cycle the device → recents persist.

- [ ] **Step 6: Commit**

```bash
git add components/recents/include/recents.h components/recents/recents.cpp \
        components/recents/CMakeLists.txt components/ui_screens/screen_recent.cpp
git commit -m "Add NVS-backed recents persistence and recent screen"
```

### Task 5.6: Throttle screen module (move logic out of ui_events.cpp)

**Files:**
- Modify: `components/ui_screens/screen_throttle.cpp` (replace stub)
- Modify: `main/ui_events.cpp` (remove the moved logic; keep generated event entry points)
- Modify: `main/main.cpp` (remove `update_main_control_state`; logic moves to the module's tick)

- [ ] **Step 1: Implement the throttle module**

Move the (now de-espnow'd) `onTrainMainControlLoaded` body and `update_main_control_state` logic into `screen_throttle.cpp`. The module:
- `enter()`: `lv_scr_load(ui_Train_Main_Control)`, build HORN/STOP/Fn styling and battery label (the code already in `ui_events.cpp`), bind STOP→`withr::emergency_stop()`, Fn→`screen_mgr_show(Screen::FUNCTIONS)`, Dir→toggle, back/home→`withr::release_loco()` + `screen_mgr_show(Screen::HOME)`, HORN press/release→`withr::horn()`.
- `tick()`: refresh loco name (`withr::get_loco_name`), direction label, battery (every 30 s), and the speed arc value from `withr::get_speed()`.
- `on_rotate(delta)`: the arc/speed code currently in `main.cpp` (`lv_arc_get/set_value` on `ui_Train_Main_Throttle`, convert to percent, `withr::set_speed`).

```cpp
#include "screen_mgr.h"
#include "withrottle_client.h"
#include "ui.h"
#include "battery_bsp.h"
#include "esp_timer.h"

static void on_stop(lv_event_t*) { withr::emergency_stop(); }
static void on_fn(lv_event_t*)   { screen_mgr_show(Screen::FUNCTIONS); }
static void on_dir(lv_event_t*)  {
    auto d = withr::get_direction();
    if (d) withr::set_direction(*d == Direction::Forward ? Direction::Reverse : Direction::Forward);
}
static void on_back(lv_event_t*) { withr::release_loco(); screen_mgr_show(Screen::HOME); }
static void on_horn(lv_event_t* e) {
    lv_event_code_t c = lv_event_get_code(e);
    withr::horn(c == LV_EVENT_PRESSED);
}

static void enter() {
    lv_scr_load(ui_Train_Main_Control);
    // (build/style STOP, Fn, HORN, battery, back — port from ui_events.cpp here,
    //  binding the handlers above; guard one-time styling with a static bool.)
}
static void tick() {
    // loco name, direction label, battery (30 s), arc value from withr::get_speed()
}
static void on_rotate(int delta) {
    int16_t cur = lv_arc_get_value(ui_Train_Main_Throttle);
    int16_t mn  = lv_arc_get_min_value(ui_Train_Main_Throttle);
    int16_t mx  = lv_arc_get_max_value(ui_Train_Main_Throttle);
    int16_t nv  = cur + (-1 * (delta / 50));
    if (nv < mn) nv = mn; if (nv > mx) nv = mx;
    lv_arc_set_value(ui_Train_Main_Throttle, nv);
    withr::set_speed((uint8_t)(((nv - mn) * 100) / (mx - mn)));
}
ScreenModule g_screen_throttle{ enter, nullptr, tick, on_rotate };
```

- [ ] **Step 2: Strip the moved code from `ui_events.cpp` and `main.cpp`**

Delete `update_main_control_state` from `main.cpp` and the throttle-specific scroll block (already routed via `screen_mgr_on_rotate`). In `ui_events.cpp`, keep only what the generated screen's event table still references; move handler bodies into the module. If the generated screen calls `onStopClicked`/`onDirClicked`/`onTrainMainControlLoaded` by name (from the SquareLine event export), keep thin forwarders in `ui_events.cpp` that the module's handlers also use, or rebind in `enter()` and leave the generated callbacks as no-ops.

- [ ] **Step 3: Build, flash, verify (manual)**

Expected: full throttle behaviour through the screen manager — encoder sets speed on the arc, STOP, direction, HORN hold, Fn opens functions, back releases the loco and returns Home.

- [ ] **Step 4: Commit**

```bash
git add components/ui_screens/screen_throttle.cpp main/ui_events.cpp main/main.cpp
git commit -m "Move throttle screen logic into ui_screens module"
```

### Task 5.7: Functions screen module (wrap existing TrainFnScreen)

**Files:**
- Move: `main/train_fn_screen.{h,cpp}` → `components/ui_screens/`
- Modify: `components/ui_screens/screen_functions.cpp` (replace stub)
- Modify: `components/ui_screens/CMakeLists.txt` (add `train_fn_screen.cpp`)

- [ ] **Step 1: Move the function-list helper into the component**

```bash
git mv main/train_fn_screen.h main/train_fn_screen.cpp components/ui_screens/
```
Update `train_fn_screen.h`'s include from `espnow_dial.h` to `withrottle_client.h` (already done in Task 3.3 if it lived in main; re-confirm). Add `train_fn_screen.cpp` to `ui_screens/CMakeLists.txt` SRCS.

- [ ] **Step 2: Implement the module wrapper**

`components/ui_screens/screen_functions.cpp`:

```cpp
#include "screen_mgr.h"
#include "train_fn_screen.h"
#include "ui.h"
#include "esp_timer.h"

static void enter() {
    lv_scr_load(ui_Train_Fn_Control);
    TrainFnScreen::load_for_loco();
}
static void leave() { TrainFnScreen::on_screen_unloaded(); }
static uint32_t s_last_refresh = 0;
static void tick() {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - s_last_refresh >= 1000) { TrainFnScreen::load_for_loco(); s_last_refresh = now; }
}
static void on_rotate(int delta) {
    if (!uic_train_fn_container) return;
    if (delta > 0 && lv_obj_get_scroll_top(uic_train_fn_container) <= 0) return;
    if (delta < 0 && lv_obj_get_scroll_bottom(uic_train_fn_container) <= 0) return;
    lv_obj_scroll_by(uic_train_fn_container, 0, (int)(1.5f * delta), LV_ANIM_OFF);
}
ScreenModule g_screen_functions{ enter, leave, tick, on_rotate };
```

- [ ] **Step 3: Build, flash, verify (manual)**

Expected: Functions screen lists named functions, taps toggle them in JMRI, encoder scrolls the list.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Wrap function-list screen in ui_screens module"
```

### Task 5.8: Settings screen module (read-only)

**Files:**
- Modify: `components/ui_screens/screen_settings.cpp` (replace stub)
- Read first: `components/ui/generated/screens/ui_Settings_Screen.h` (confirm `uic_Wifi_Value`, `uic_IP_Value`, `uic_withrottle_url_value`)

- [ ] **Step 1: Implement the module**

Port `update_settings_screen_values()` from `main.cpp` into the module's `tick()`:

```cpp
#include "screen_mgr.h"
#include "withrottle_client.h"
#include "wifi.h"
#include "ui.h"

static void enter() { lv_scr_load(ui_Settings_Screen); }
static void tick() {
    if (uic_Wifi_Value) lv_label_set_text(uic_Wifi_Value, get_current_ssid().c_str());
    if (uic_IP_Value)   lv_label_set_text(uic_IP_Value,   get_current_ip_address().c_str());
    if (uic_withrottle_url_value) {
        std::string url = withr::get_server_url();
        if (!url.empty()) lv_label_set_text(uic_withrottle_url_value, url.c_str());
    }
}
ScreenModule g_screen_settings{ enter, nullptr, tick, nullptr };
```

Remove `update_settings_screen_values()` from `main.cpp`. Add a Settings back/home button binding (read the header; if a back button exists, bind it to `screen_mgr_show(Screen::HOME)`; otherwise add one).

- [ ] **Step 2: Build, flash, verify (manual)**

Expected: Settings shows live SSID / IP / JMRI URL; back returns Home.

- [ ] **Step 3: Commit**

```bash
git add components/ui_screens/screen_settings.cpp main/main.cpp
git commit -m "Add read-only settings screen module"
```

---

## Phase 6 — Configuration, naming and docs

### Task 6.1: Compile-time secrets header

**Files:**
- Create: `src/secrets.h.example`
- Create: `src/secrets.h` (git-ignored — already in `.gitignore`)
- Modify: `components/withrottle_client/withrottle_client.cpp` and `components/wifi/*` to fall back to secrets defaults

- [ ] **Step 1: Create the example header**

`src/secrets.h.example`:

```cpp
#pragma once
// Copy to src/secrets.h and fill in. src/secrets.h is git-ignored.
#define SECRET_WIFI_SSID     "your-ssid"
#define SECRET_WIFI_PASSWORD "your-password"
#define SECRET_JMRI_IP       "192.168.1.7"
#define SECRET_JMRI_PORT     12090
```

- [ ] **Step 2: Create the real header**

```bash
cp src/secrets.h.example src/secrets.h
# edit src/secrets.h with real values
```

- [ ] **Step 3: Use the defaults as the Param fallback**

In `withrottle_client.cpp`, include `"secrets.h"` and change the param defaults:

```cpp
params::Param<std::string> s_jmri_ip{"jmri_ip", std::string(SECRET_JMRI_IP)};
params::Param<int>         s_jmri_port{"jmri_port", SECRET_JMRI_PORT};
```

(Rename done in Task 6.2.) Ensure the `native` env never includes `secrets.h` (it only compiles the pure-logic files, which don't).

- [ ] **Step 4: Add `src` to the project include path for the firmware build**

In `platformio.ini` under `[env:waveshare_knob]`, add `build_flags = -I src`. Confirm the wifi component reads SالسSID/password from NVS first, secrets as fallback (mirror the jmri_ip change in `components/wifi`).

- [ ] **Step 5: Build, flash, verify (manual)**

Expected: with NVS unset, the device uses the secrets defaults; `param set jmri_ip …; param save` overrides them.

- [ ] **Step 6: Commit (only the example, never src/secrets.h)**

```bash
git add src/secrets.h.example platformio.ini components/withrottle_client/withrottle_client.cpp
git status   # confirm src/secrets.h is NOT staged
git commit -m "Add compile-time secrets defaults with NVS override"
```

### Task 6.2: Rename NVS params to jmri_ip / jmri_port and gate udp_log

**Files:**
- Modify: `components/withrottle_client/withrottle_client.cpp`
- Modify: `main/main.cpp` (udp_log)

- [ ] **Step 1: Rename the params**

Rename `s_withr_ip`/`"withr_ip"` → `s_jmri_ip`/`"jmri_ip"` and `s_withr_port`/`"withr_port"` → `s_jmri_port`/`"jmri_port"` throughout `withrottle_client.cpp`, including `get_server_url()`.

- [ ] **Step 2: Make the UDP log host an optional param**

In `main.cpp`, replace `udp_log_start("192.168.1.160", 4444);` with:

```cpp
static params::Param<std::string> s_udp_log_ip{"udp_log_ip", std::string("")};
if (!s_udp_log_ip.get().empty()) udp_log_start(s_udp_log_ip.get().c_str(), 4444);
```

Add `#include "Param.h"` if not present.

- [ ] **Step 3: Build, flash, verify (manual)**

Expected: builds; `param list` shows `jmri_ip`, `jmri_port`, `udp_log_ip`; no UDP log spam when `udp_log_ip` is empty.

- [ ] **Step 4: Commit**

```bash
git add components/withrottle_client/withrottle_client.cpp main/main.cpp
git commit -m "Rename JMRI params and gate UDP logging behind a param"
```

### Task 6.3: README and on-device verification checklist

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Rewrite the README**

Replace the dial's ESP-NOW-proxy README with: project summary, hardware, features, navigation map (from the spec), configuration (`secrets.h` + `param set` keys: `wifi_ssid`, `wifi_password`, `jmri_ip`, `jmri_port`, `udp_log_ip`), build/flash/OTA commands (`pio run -e waveshare_knob`, `-e waveshare_knob_usb -t upload`, `./build.sh ota`), and the manual verification checklist below.

- [ ] **Step 2: Include the manual verification checklist**

```
[ ] Boot → connecting screen steps Wi-Fi → JMRI → roster → Home
[ ] Roster lists locos, sorted by name; encoder scrolls
[ ] Tap loco → acquires, shows on throttle, name correct
[ ] Encoder rotates → speed arc tracks, JMRI throttle moves
[ ] Direction toggles F/R
[ ] HORN: hold sounds, release silences (correct function)
[ ] Fn screen lists named functions; tap toggles in JMRI
[ ] STOP → emergency stop, arc red
[ ] Back → loco released in JMRI, returns Home
[ ] Recents lists prior locos; persists across power cycle
[ ] Drop Wi-Fi → connecting screen returns; reconnect recovers
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "Rewrite README for direct JMRI WiThrottle build"
```

### Task 6.4: Reconnect-to-Connecting fallback

**Files:**
- Modify: `components/ui_screens/screen_throttle.cpp`, `screen_functions.cpp`

- [ ] **Step 1: Fall back to the connecting screen on connection loss**

In the Throttle and Functions `tick()`, if `withr::get_phase() != withr::Phase::READY` (e.g. Wi-Fi/JMRI dropped), call `screen_mgr_show(Screen::CONNECTING)`. The connecting screen already auto-advances back to Home when `READY` returns.

- [ ] **Step 2: Build, flash, verify (manual)**

Expected: pulling Wi-Fi while driving returns to the connecting screen; restoring Wi-Fi/JMRI advances back to Home without a reboot.

- [ ] **Step 3: Commit**

```bash
git add components/ui_screens/screen_throttle.cpp components/ui_screens/screen_functions.cpp
git commit -m "Return to connecting screen on connection loss"
```

---

## Self-review (filled in by the plan author)

**Spec coverage:**
- Roster browser → Task 5.4. Recents (NVS) → 2.3 + 5.5. Horn (auto-detect) → 2.2 + 3.2. Speed arc/direction/speed% → 5.6. E-stop → 5.6. Functions (32) → 5.7. Single loco → enforced (one multiThrottle slot, no badge). Touch-first input → 5.6/5.7 (touch handlers) + encoder rotation routing 4.2. Connecting screen → 5.2. Settings read-only → 5.8. Config (NVS + serial + secrets) → 6.1/6.2. Error handling/reconnect → 6.4. Testing → Phase 1–2 native tests + 6.3 checklist. ESP-NOW removed → 3.3.
- Section 9 risks: LVGL 8.4 (we stay on the dial's 8.4 throughout); WiThrottleProtocol submodule (Task 0.1 verifies it); large-roster lazy rendering (deferred, noted).

**Placeholder scan:** The two `TODO`-style notes (Home Task 5.3, Throttle Task 5.6) are explicit read-the-generated-header-then-bind instructions tied to a prior step's output, not open scope. All pure-logic tasks contain complete code; UI tasks give complete handler code and name the exact generated symbols to bind.

**Type consistency:** `LocoRef{address:int, length:char, name:string}` is used identically across `loco_ref`, `recents_serialize`, `recents`, `list_widget`, and the screens. `withr::` signatures (`acquire_loco(int,char)`, `get_roster()→vector<LocoRef>`, `get_phase()→Phase`, `horn(bool)`, `get_horn_fn()→int`) match between declaration (3.1), implementation (3.2), and all call sites (5.x). Recents API names: `recents_push`/`recents_serialize`/`recents_deserialize` (pure) vs `recents_load`/`recents_store_push` (NVS) — distinct and used consistently.
