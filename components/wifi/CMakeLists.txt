idf_component_register(
  SRCS "wifi.cpp"

  PRIV_REQUIRES
    "esp_event"
    "nvs_flash"

  REQUIRES
    "esp_wifi"
    "param"
    "console"
    "esp_pm"
    "wpa_supplicant"
    "lwip"

  INCLUDE_DIRS "./" "../../src")
