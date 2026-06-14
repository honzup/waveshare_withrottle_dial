#ifndef ESP_WIFI_BSP_H
#define ESP_WIFI_BSP_H
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h" //WIFI

void        espwifi_Init(void);
bool        is_wifi_connected();
int         get_wifi_rssi();
std::string get_current_ssid();
std::string get_current_ip_address();
bool        wifi_has_credentials();    // true if both wifi_ssid and wifi_password are non-empty
void        wifi_suspend_sta(bool on); // when on, the disconnect handler stops auto-reconnecting

#endif // ESP_WIFI_BSP_H
