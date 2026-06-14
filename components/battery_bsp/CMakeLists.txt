idf_component_register(
    SRCS "battery_bsp.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc esp_driver_usb_serial_jtag log
)
