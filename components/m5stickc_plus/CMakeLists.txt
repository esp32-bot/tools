idf_component_register(
    SRCS "i2c_device.cpp" "axp192.cpp" "mpu6886.cpp" "display.cpp" "speaker.cpp" "led.cpp" "button.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c esp_driver_gpio esp_lcd
    PRIV_REQUIRES esp_driver_spi esp_driver_ledc)
