idf_component_register(
    SRCS "ota.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES console_commands esp_https_ota esp_http_client app_update esp-tls)
