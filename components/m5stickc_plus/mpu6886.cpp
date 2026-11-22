#include "m5stickc_plus/mpu6886.h"

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "m5stickc_plus/pins.h"
#include "mpu6886_registers.h"

namespace m5 {

using namespace mpu6886;

namespace {

struct RegisterValue {
    uint8_t reg;
    uint8_t value;
};

constexpr RegisterValue SETUP[] = {
    {SAMPLE_RATE_DIVIDER, RATE_166_HZ},
    {CONFIG, GYRO_FILTER_176_HZ},
    {GYRO_CONFIG, GYRO_250_DPS},
    {ACCEL_CONFIG, ACCEL_2_G},
    {ACCEL_CONFIG_2, ACCEL_FILTER_218_HZ},
};

}  // namespace

esp_err_t Mpu6886::init(i2c_master_bus_handle_t bus) {
    esp_err_t err = device_.init(bus, MPU6886_ADDRESS, INTERNAL_I2C_HZ);
    uint8_t id = 0;
    if (err == ESP_OK) {
        err = device_.read(WHO_AM_I, &id, 1);
    }
    if (err == ESP_OK && id != WHO_AM_I_VALUE) {
        err = ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_OK) {
        err = device_.write(PWR_MGMT_1, DEVICE_RESET);
        vTaskDelay(pdMS_TO_TICKS(RESET_MS));
    }
    if (err == ESP_OK) {
        err = device_.write(PWR_MGMT_1, CLOCK_AUTO);
        vTaskDelay(pdMS_TO_TICKS(WAKE_MS));
    }
    for (const auto& [reg, value] : SETUP) {
        if (err == ESP_OK) {
            err = device_.write(reg, value);
        }
    }
    return err;
}

esp_err_t Mpu6886::read(Motion& out) const {
    uint8_t data[MOTION_WORDS * 2];
    const esp_err_t err = device_.read(ACCEL_XOUT_H, data, sizeof data);
    if (err != ESP_OK) {
        return err;
    }
    const auto word = [&](int index) { return static_cast<int16_t>((data[2 * index] << 8) | data[2 * index + 1]); };
    out.accel_g = {word(0) * G_PER_LSB, word(1) * G_PER_LSB, word(2) * G_PER_LSB};
    out.temperature_c = word(TEMP_WORD) / TEMP_LSB_PER_C + TEMP_OFFSET_C;
    out.gyro_dps = {word(GYRO_WORD) * DPS_PER_LSB, word(GYRO_WORD + 1) * DPS_PER_LSB,
                    word(GYRO_WORD + 2) * DPS_PER_LSB};
    return ESP_OK;
}

}  // namespace m5
