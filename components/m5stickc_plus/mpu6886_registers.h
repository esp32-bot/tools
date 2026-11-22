#pragma once

// The MPU6886 registers this driver uses, from the MPU6886 datasheet.

#include <cstdint>

namespace m5::mpu6886 {

constexpr uint8_t WHO_AM_I = 0x75;
constexpr uint8_t WHO_AM_I_VALUE = 0x19;

constexpr uint8_t PWR_MGMT_1 = 0x6b;
constexpr uint8_t DEVICE_RESET = 1 << 7;
constexpr uint8_t CLOCK_AUTO = 0x01;  // the gyro PLL once it's running
constexpr uint32_t RESET_MS = 10;
constexpr uint32_t WAKE_MS = 10;

// Measuring: the settings M5Stack's library uses, then the most sensitive ranges, which a
// balancing robot never leaves.
constexpr uint8_t SAMPLE_RATE_DIVIDER = 0x19;
constexpr uint8_t RATE_166_HZ = 5;  // 1 kHz / (1 + 5)
constexpr uint8_t CONFIG = 0x1a;
constexpr uint8_t GYRO_FILTER_176_HZ = 1;
constexpr uint8_t GYRO_CONFIG = 0x1b;
constexpr uint8_t GYRO_250_DPS = 0;
constexpr uint8_t ACCEL_CONFIG = 0x1c;
constexpr uint8_t ACCEL_2_G = 0;
constexpr uint8_t ACCEL_CONFIG_2 = 0x1d;
constexpr uint8_t ACCEL_FILTER_218_HZ = 0;

// Accelerometer x, y, z, temperature, then gyro x, y, z: each a big-endian int16.
constexpr uint8_t ACCEL_XOUT_H = 0x3b;
constexpr int MOTION_WORDS = 7;
constexpr int TEMP_WORD = 3;
constexpr int GYRO_WORD = 4;
constexpr float G_PER_LSB = 2.0f / 32768;      // at ACCEL_2_G
constexpr float DPS_PER_LSB = 250.0f / 32768;  // at GYRO_250_DPS
constexpr float TEMP_LSB_PER_C = 326.8f;
constexpr float TEMP_OFFSET_C = 25.0f;

}  // namespace m5::mpu6886
