# Third-Party Notices

This project bundles and/or depends on the components listed below, each under
its own license. The MIT license in `LICENSE` covers only this project's own
code; the components below retain their respective licenses.

| Component | Where | License |
|-----------|-------|---------|
| **WiThrottleProtocol** | git submodule `components/withrottle/` → [`honzup/WiThrottleProtocol`](https://github.com/honzup/WiThrottleProtocol) (`esp-idf-component` branch), forked from [`flash62au/WiThrottleProtocol`](https://github.com/flash62au/WiThrottleProtocol) | **CC BY-SA 4.0** — see `LICENSES/CC-BY-SA-4.0.txt` and the submodule's own `LICENSE` |
| **LVGL** (graphics library) | managed dependency `lvgl/lvgl` 8.4.x | MIT |
| **ESP-IDF** components (FreeRTOS port, esp_wifi, esp_http_server, lwip, mdns, nvs_flash, esp_lcd, …) | ESP-IDF 5.3.2 + `espressif/mdns` | Apache-2.0 |
| **esp_lcd_sh8601** panel driver | managed dependency `esp_lcd_sh8601` | Apache-2.0 |
| **Unity** test framework | `test/native/unity/` | MIT |
| **argtable3** (console arg parsing, via ESP-IDF) | ESP-IDF | BSD-3-Clause |
| **SquareLine UI design** (home / roster / settings screens) | `sq_studio_prj/`, `components/ui/generated/` | **MIT** — © 2026 len0rd; see `LICENSES/MIT-len0rd.txt` |
| Display / touch / BSP bring-up | `components/ui/`, `components/hw_config/`, `components/*_bsp/` | Derived from Espressif / Waveshare examples (Apache-2.0 / MIT) |

## WiThrottleProtocol — Attribution (CC BY-SA 4.0)

`components/withrottle/` is a **git submodule** pointing at
[`honzup/WiThrottleProtocol`](https://github.com/honzup/WiThrottleProtocol)
(`esp-idf-component` branch) — a fork of the upstream
[`flash62au/WiThrottleProtocol`](https://github.com/flash62au/WiThrottleProtocol)
Arduino library (authors: Peter Akers, David Zuhn, Luca Dentella; original
WiThrottleProtocol © Blue Knobby Systems). The fork adds an ESP-IDF
`CMakeLists` and a small Arduino-compatibility shim (`compat/`), an adaptation
originally contributed by **@len0rd**. The library is licensed under the
**Creative Commons Attribution-ShareAlike 4.0 International License
(CC BY-SA 4.0)**.

- You must give appropriate credit and indicate if changes were made.
- If you remix, transform, or build upon this component, you must distribute
  your contributions to **that component** under the same CC BY-SA 4.0 license.

The licence text is included at `LICENSES/CC-BY-SA-4.0.txt`, and the upstream
`LICENSE` file is preserved inside the submodule at `components/withrottle/LICENSE`.

## Origin & Credits

This project began from **[len0rd/withrottle_dial](https://github.com/len0rd/withrottle_dial)**
by **Tyler Miller (@len0rd)** — the original proof-of-concept WiThrottle
controller for the Waveshare ESP32 dial. The application code (WiThrottle
client, screen manager, settings/console, provisioning, recents, OTA and the
firmware entry point) has since been **independently re-authored** for this
project. What still originates directly from @len0rd's work is:

- the **SquareLine UI design** — `sq_studio_prj/` and the generated screens in
  `components/ui/generated/` (home / roster / settings), **© 2026 len0rd, MIT**; and
- the **ESP-IDF adaptation** of WiThrottleProtocol, now carried in the
  `honzup/WiThrottleProtocol` submodule fork under CC BY-SA 4.0 (see above).

> **Licensing:** `len0rd/withrottle_dial` is licensed **MIT** (© 2026 len0rd) as
> of 2026-06-14. The SquareLine UI design above is used under that MIT licence
> with attribution (see `LICENSES/MIT-len0rd.txt`). The application code was
> independently re-authored before the licence was granted and is the project's
> own under the top-level MIT `LICENSE`.

- Functionality also modelled on **honzup/m5stack_wiThrottle**.
