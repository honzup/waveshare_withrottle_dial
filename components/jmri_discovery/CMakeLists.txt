idf_component_register(SRCS "choose_server.cpp" "jmri_discovery.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES mdns esp_netif)
