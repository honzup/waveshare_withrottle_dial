idf_component_register(
  SRCS
    "IParam.cpp"
    "NvsMgr.cpp"
    "ParamMgr.cpp"

  INCLUDE_DIRS
    "."

  REQUIRES
    "console_commands"
    "nvs_flash"
)
