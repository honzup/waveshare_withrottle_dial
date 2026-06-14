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
