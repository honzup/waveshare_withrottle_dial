idf_component_register(
    SRCS
        "main.cpp"
        "ui_events.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        hw_config
        withrottle_client
        ui_screens
        ota_server
        udp_log
        wifi
        user_encoder_bsp
        ui
        battery_bsp
    )

target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,-u,onStopClicked")

set_source_files_properties(
    ${LV_DEMOS_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE)
