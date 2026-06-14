idf_component_register(
    SRCS "screen_mgr.cpp"
         "screen_splash.cpp"
         "screen_connecting.cpp" "screen_home.cpp" "screen_roster.cpp"
         "screen_recent.cpp" "screen_throttle.cpp" "screen_functions.cpp"
         "screen_settings.cpp" "screen_provisioning.cpp" "list_widget.cpp" "train_fn_screen.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ui withrottle_client loco_ref withrottle recents wifi battery_bsp esp_app_format jmri_discovery netprov)
