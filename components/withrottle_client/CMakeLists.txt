idf_component_register(SRCS "withrottle_client.cpp"
                       INCLUDE_DIRS "." "../../src"
                       REQUIRES withrottle wifi param loco_ref horn_resolver jmri_discovery)
