idf_component_register(
    SRCS "settings.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
    PRIV_REQUIRES console_commands)
