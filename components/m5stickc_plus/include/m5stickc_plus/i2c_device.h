#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace m5 {

esp_err_t new_i2c_bus(i2c_port_num_t port, gpio_num_t sda, gpio_num_t scl, i2c_master_bus_handle_t* bus);

// A device on an I2C bus whose registers have 8-bit addresses.
class I2cDevice {
public:
    esp_err_t init(i2c_master_bus_handle_t bus, uint16_t address, uint32_t scl_hz);
    // Reads `size` bytes starting at `reg`, in one transaction.
    esp_err_t read(uint8_t reg, uint8_t* data, size_t size) const;
    esp_err_t write(uint8_t reg, uint8_t value) const;
    // Clears the bits in `clear`, then sets the bits in `set`.
    esp_err_t update(uint8_t reg, uint8_t clear, uint8_t set) const;

private:
    i2c_master_dev_handle_t handle_ = nullptr;
};

}  // namespace m5
