#include "m5stickc_plus/i2c_device.h"

namespace m5 {

namespace {

constexpr int TIMEOUT_MS = 10;
constexpr uint8_t GLITCH_FILTER_CYCLES = 7;  // the driver's recommended value

}  // namespace

esp_err_t new_i2c_bus(i2c_port_num_t port, gpio_num_t sda, gpio_num_t scl, i2c_master_bus_handle_t* bus) {
    i2c_master_bus_config_t config = {};
    config.i2c_port = port;
    config.sda_io_num = sda;
    config.scl_io_num = scl;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = GLITCH_FILTER_CYCLES;
    config.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&config, bus);
}

esp_err_t I2cDevice::init(i2c_master_bus_handle_t bus, uint16_t address, uint32_t scl_hz) {
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = scl_hz;
    return i2c_master_bus_add_device(bus, &config, &handle_);
}

esp_err_t I2cDevice::read(uint8_t reg, uint8_t* data, size_t size) const {
    return i2c_master_transmit_receive(handle_, &reg, 1, data, size, TIMEOUT_MS);
}

esp_err_t I2cDevice::write(uint8_t reg, uint8_t value) const {
    const uint8_t frame[] = {reg, value};
    return i2c_master_transmit(handle_, frame, sizeof frame, TIMEOUT_MS);
}

esp_err_t I2cDevice::update(uint8_t reg, uint8_t clear, uint8_t set) const {
    uint8_t value;
    esp_err_t err = read(reg, &value, 1);
    if (err != ESP_OK) {
        return err;
    }
    return write(reg, (value & ~clear) | set);
}

}  // namespace m5
