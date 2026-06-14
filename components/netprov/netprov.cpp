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

// netprov_start/stop/is_active are called only from the UI task; not reentrant.
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
