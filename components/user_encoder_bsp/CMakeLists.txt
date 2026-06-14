idf_component_register(
  SRCS "src/bidi_switch_knob.c" "user_encoder_bsp.c"
  REQUIRES hw_config driver
  INCLUDE_DIRS "./" "./src")
