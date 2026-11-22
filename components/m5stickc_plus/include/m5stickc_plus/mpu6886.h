#pragma once

#include "esp_err.h"
#include "m5stickc_plus/i2c_device.h"

namespace m5 {

struct Vector3 {
    float x, y, z;
};

// One reading of the IMU, in its own axes (printed on the M5StickC Plus's back).
struct Motion {
    Vector3 accel_g;
    Vector3 gyro_dps;
    float temperature_c;
};

// The MPU6886 6-axis IMU, set to its most sensitive ranges: ±2 g and ±250 °/s.
class Mpu6886 {
public:
    // Resets and sets up the IMU; fails if it isn't an MPU6886.
    esp_err_t init(i2c_master_bus_handle_t bus);
    // Everything at once, in one bus transaction.
    esp_err_t read(Motion& out) const;

private:
    I2cDevice device_;
};

}  // namespace m5
