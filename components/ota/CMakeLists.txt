idf_component_register(
    SRCS "ota.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES console console_commands esp_https_ota esp_http_client esp_http_server app_update esp-tls mbedtls esp_timer)
