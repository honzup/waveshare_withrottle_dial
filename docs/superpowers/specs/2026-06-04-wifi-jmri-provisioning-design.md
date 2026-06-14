# Wi-Fi & JMRI Provisioning Design

**Status:** Approved (brainstorming) — ready for implementation planning.

**Goal:** Let the user configure Wi-Fi credentials and locate the JMRI WiThrottle
server without recompiling, replacing the compile-time `SECRET_*` hardcoding with
an on-device flow: SoftAP captive-portal Wi-Fi provisioning plus mDNS auto-discovery
of the JMRI server.

## Background

The `SECRET_*` macros are only **compile-time defaults**. Each is already wrapped in
a `params::Param<T>` backed by NVS (`components/param`), so a value written to flash
overrides the default. Three pieces already exist:

- Persistent storage — `params::Param` → `NvsMgr` (NVS).
- Change config without recompiling — serial console `param set <name> <value>`
  (`ParamMgr.cpp`).
- Apply without reboot — Wi-Fi `cmd_do_sta_connect()` is runtime-callable; the
  WiThrottle connect loop re-reads `s_jmri_ip.get()` / `s_jmri_port.get()` on every
  reconnect (`withrottle_client.cpp`).

What is missing is the **front-end** to set those params, and a way to do it on a
360×360 round touchscreen with no physical keyboard. This design avoids on-screen
text entry: Wi-Fi credentials are entered from a phone browser (captive portal) and
the JMRI server is discovered automatically (mDNS).

The four params reused (no new storage mechanism is introduced):
`wifi_ssid`, `wifi_password`, `jmri_ip`, `jmri_port`. The `SECRET_*` values remain
as last-resort defaults.

## Architecture & Scope

Two **independent** features on the `feat/wifi-jmri-provisioning` branch, built as
two phases (either could ship alone):

- **Phase A — JMRI auto-discovery (mDNS).** A `jmri_discovery` module browses
  `_withrottle._tcp.local`. The WiThrottle connect loop calls it first; the saved
  `jmri_ip`/`jmri_port` params become a fallback.
- **Phase B — Wi-Fi provisioning (SoftAP captive portal).** A `netprov` component
  brings up an open AP (`WiThrottle-Setup`), a catch-all DNS responder (so the
  phone's "sign in to network" page opens automatically), and an `esp_http_server`
  serving a config form. On submit it writes the params to NVS and reboots into
  normal STA mode.

**HTTP port coexistence (resolved):** `ota_server_start()` is a custom espota task
listening on **UDP 3232** (`ESPOTA_PORT`), not an HTTP server. The captive portal's
`esp_http_server` binds TCP :80, so there is **no port clash** — the two coexist and
need no gating. (Provisioning is still only entered when not normally connected, but
that is a flow decision, not a port constraint.)

## Components & Files

### New component: `components/jmri_discovery/` (Phase A)

- `jmri_discovery.h/.cpp` — `std::vector<JmriServer> discover(uint32_t timeout_ms)`
  where `struct JmriServer { std::string name; std::string ip; uint16_t port; }`.
  Wraps `mdns_init` + `mdns_query_ptr("_withrottle", "_tcp", …)`.
- `choose_server.h/.cpp` — **pure, unit-testable** selection logic:
  `std::optional<JmriServer> choose(const std::vector<JmriServer>& found,
  const std::string& saved_ip, uint16_t saved_port)`. Returns the single match, or
  the saved IP as a synthetic fallback when the list is empty. A multi-result list
  is reported as "ambiguous" so the caller can show a picker.
- Adds `espressif/mdns` to `main/idf_component.yml`.

### New component: `components/netprov/` (Phase B)

- `netprov.h/.cpp` — lifecycle: `netprov_start()` (AP up in `WIFI_MODE_APSTA` so it
  can also scan for networks), `netprov_stop()`, `netprov_is_active()`.
- `captive_dns.cpp` — minimal UDP:53 responder answering every query with the AP IP
  (192.168.4.1).
- `portal_http.cpp` — `esp_http_server`: `GET /` serves the form; `GET /scan`
  returns nearby SSIDs (JSON from `esp_wifi_scan`); `POST /save` validates + writes
  params + responds "saved, restarting" + `esp_restart()`. Catch-all 302 → `/` for
  OS captive-portal probe URLs.
- `validation.h/.cpp` — **pure, unit-testable**: `bool valid_ipv4(std::string)`,
  `bool valid_port(std::string, uint16_t& out)`, SSID/password length checks.
- Static HTML/CSS embedded as a string constant (small, no filesystem).

### Touched existing files

- `components/wifi/wifi.cpp` — expose `wifi_has_credentials()` and a
  connection-failure signal (`wifi_connect_failed_for(ms)` or equivalent); call
  `netprov_start()` on the no-creds / repeated-failure path.
- `components/withrottle_client/withrottle_client.cpp` — connect loop calls
  `jmri_discovery::discover()` → `choose()` before dialling; remembers a discovered
  IP via the `jmri_ip` param.
- `main/main.cpp` — wire the boot decision; ensure portal vs OTA HTTP servers do not
  both bind :80.
- **UI:**
  - new `components/ui_screens/screen_provisioning.cpp` — instructions + AP name,
    shown while `netprov_is_active()`.
  - `screen_settings.cpp` — add a "Reconfigure Wi-Fi" button; if discovery returns
    several servers, a JMRI picker.
  - `screen_connecting.cpp` — route into provisioning after the failure timeout.

**Design principle:** network/hardware-bound bits (mDNS browse, AP, HTTP, DNS) are
thin wrappers; the branchy **decision logic** (`choose_server`, `validation`) is
pure and unit-testable on the native target via `test/native`.

## State Machine & Data Flow

```
            ┌─ no creds stored ─────────────────────────────┐
boot ─► check creds                                          ▼
            └─ creds present ─► STA connect ─ success ─► JMRI discovery ─► THROTTLE/HOME
                                   │                          │
                                   │ fail > 30s               │ (mDNS-first)
                                   ▼                          ▼
                              PROVISIONING ◄── manual ──  none found ─► saved IP fallback
                              (SoftAP + portal)            one found ─► use + remember
                                   │                       many found ─► picker screen
                              POST /save ─► write params ─► esp_restart()
```

**Provisioning flow:** phone joins `WiThrottle-Setup` → DNS hijack → portal `GET /`
→ user picks SSID (from `/scan`), enters password, optionally overrides JMRI
host/port → `POST /save` → `validation` → `ParamMgr::setParam(...)` for each field
(persists to NVS) → "Saved, restarting" page → `esp_restart()`. On reboot, creds now
exist → normal STA path. **Reboot, not hot-switch** — simplest and most robust; the
params already survive reboot.

**Discovery flow:** connect loop → `discover(3000ms)` → `choose(found, saved_ip,
saved_port)`. Exactly one → connect and write it back to the `jmri_ip` param so it is
remembered. Several → post the list to a UI picker, connect to the chosen one. Zero →
fall back to saved IP/port and keep retrying as today.

## Error Handling

- **mDNS blocked / nothing found** → silent fallback to saved IP; no user-facing
  failure (never strands the user).
- **Saved IP also unusable** (placeholder/empty) → stay in the SERVER-waiting phase,
  retrying; the manual "Reconfigure Wi-Fi" path remains available.
- **Bad form input** → `POST /save` returns the form with an inline error; nothing
  written, no reboot.
- **Provisioning timeout** → none; the AP stays up until configured (a half-set
  device must not silently give up).
- **Wrong Wi-Fi password** → device reboots, STA fails for 30s, falls back into
  provisioning automatically. Self-correcting.
- **HTTP port clash** → portal binds :80 only while the OTA server is stopped
  (provisioning and normal-run are mutually exclusive states).

## Testing

Following the existing split (`bash test/native/run.sh` for native unit tests,
on-device for the rest).

**Native unit tests** (pure logic, no IDF/network):

- `choose_server`: empty list → saved-IP fallback; single match → that server;
  multiple → "ambiguous" signal; saved IP empty + empty list → no candidate.
- `validation`: `valid_ipv4` (accepts `192.168.1.46`; rejects `192.168.1`,
  `999.1.1.1`, `abc`, empty); `valid_port` (accepts `1`–`65535`; rejects `0`,
  `70000`, non-numeric); SSID/password length bounds.
- Param round-trip is already covered by the existing param mechanism; add a focused
  test only if a gap appears.

**On-device / manual** (network- and hardware-bound):

- First boot, no creds → AP appears, captive page auto-opens, submit → reconnects to
  real Wi-Fi.
- Wrong password → auto-returns to provisioning after the timeout.
- mDNS: JMRI found automatically with no saved IP; mDNS-blocked network → falls back
  to saved IP.
- "Reconfigure Wi-Fi" button mid-session → drops to provisioning.
- Regression: normal boot with valid stored creds connects exactly as today (no
  provisioning detour).

## Out of Scope / Notes

- Password is stored in plain NVS (as today via serial). Acceptable for a hobby
  device; noted for awareness.
- No on-device on-screen keyboard is added; text entry happens on the phone.
- BLE provisioning and the Espressif unified-provisioning app were considered and
  rejected in favour of the app-free captive portal.
