#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ADC1 channel 0 (GPIO1) via a 1:1 voltage divider.
// Readable range: ~3.0 V (empty) to ~4.2 V (full) for a single-cell LiPo.
void  battery_bsp_init(void);
float battery_get_voltage(void);   // returns battery voltage in volts
int   battery_get_pct(void);       // returns 0–100
// Returns true when a USB host (computer) is connected via the USB serial/JTAG port.
// USB wall chargers without data lines are not detected.
bool  battery_is_charging(void);

#ifdef __cplusplus
}
#endif
