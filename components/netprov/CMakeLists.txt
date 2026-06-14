idf_component_register(SRCS "netprov_validation.cpp" "netprov.cpp"
                            "captive_dns.cpp" "portal_http.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_netif esp_http_server lwip nvs_flash param wifi)
