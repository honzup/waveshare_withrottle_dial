idf_component_register(
    SRCS "ota_server.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_update log esp_system freertos
)
